// asuka/apps on a Mac: the .app bundles where Spotlight finds them, named
// by Launch Services (FileManager's displayName(atPath:)), their icons
// NSWorkspace's, drawn into a bitmap, and opened by NSWorkspace; the search
// is the ranking every desktop host answers it with (platform/linux's and
// platform/windows's apps.c).
import AppKit
import CryptoKit

@MainActor enum AsukaApps {
    static func methods() -> [String: AsukaModuleMethod] {
        [
            "list": { _, reply in
                let dirs = appDirs()
                DispatchQueue.global(qos: .userInitiated).async {
                    let out = tidy(list(dirs)).map { ["name": $0.name, "id": $0.id, "path": $0.path] }
                    reply(Result { try JSONSerialization.data(withJSONObject: out, options: [.sortedKeys]) })
                }
            },
            "icon": { args, reply in icon(args, reply) },
            "open": { args, reply in open(String(decoding: args, as: UTF8.self), reply) },
            // asuka/apps' search: {"names", "query"}, the indices best first.
            "search": { args, reply in
                DispatchQueue.global(qos: .userInitiated).async {
                    guard let o = (try? JSONSerialization.jsonObject(with: args)) as? [String: Any] else {
                        return reply(.failure(AsukaModuleError("apps.search: bad arguments")))
                    }
                    let names = o["names"] as? [String] ?? []
                    let query = o["query"] as? String ?? ""
                    reply(Result { try JSONSerialization.data(withJSONObject: AsukaAppSearch.rank(names, query)) })
                }
            },
        ]
    }

    struct App {
        var name: String
        var id: String
        var path: String
    }

    /// The folders apps are installed in, as Spotlight and Launchpad find
    /// them (ASUKA_APP_DIRS, colon-separated, in their place: the host's
    /// tests).
    static func appDirs() -> [String] {
        if let d = ProcessInfo.processInfo.environment["ASUKA_APP_DIRS"], !d.isEmpty {
            return d.split(separator: ":").map(String.init)
        }
        return ["/Applications", "/System/Applications", "/System/Library/CoreServices/Applications",
                NSHomeDirectory() + "/Applications"]
    }

    /// Finder, which lives with the system's services.
    nonisolated static let finder = "/System/Library/CoreServices/Finder.app"

    /// The .app bundles in dirs and in their folders one level down
    /// (Utilities, an app's own folder), never inside a bundle, then
    /// Finder: each once, where it is, or an app only a symbolic link
    /// points to (/Applications/Safari.app) at the link. Each is named as
    /// Finder names it, in the user's language, without .app; its ID is its
    /// bundle identifier, or its path without one. Off the main thread.
    nonisolated static func list(_ dirs: [String]) -> [App] {
        let fm = FileManager.default
        func isApp(_ name: String) -> Bool { name.hasSuffix(".app") && !name.hasPrefix(".") }
        func isDir(_ p: String) -> Bool {
            var d: ObjCBool = false
            return fm.fileExists(atPath: p, isDirectory: &d) && d.boolValue
        }
        var found: [String] = []
        for dir in dirs {
            for e in ((try? fm.contentsOfDirectory(atPath: dir)) ?? []).sorted() {
                let p = (dir as NSString).appendingPathComponent(e)
                if isApp(e) {
                    found.append(p)
                    continue
                }
                if e.hasPrefix(".") || !isDir(p) { continue }
                for s in ((try? fm.contentsOfDirectory(atPath: p)) ?? []).sorted() where isApp(s) {
                    found.append((p as NSString).appendingPathComponent(s))
                }
            }
        }
        found.append(finder)
        // Each app once: where it is, else at the first link to it.
        var paths: [String] = []
        var seen = Set<String>()
        for links in [false, true] {
            for p in found {
                guard let attrs = try? fm.attributesOfItem(atPath: p) else { continue }
                if (attrs[.type] as? FileAttributeType == .typeSymbolicLink) != links { continue }
                let real = (p as NSString).resolvingSymlinksInPath
                if seen.contains(real) || !isDir(real) { continue }
                seen.insert(real)
                paths.append(p)
            }
        }
        var out: [App] = []
        for p in paths {
            autoreleasepool {
                guard let b = Bundle(path: p) else { return }
                // Finder shows .app where the user has it show every
                // extension; Spotlight and Launchpad never do.
                var name = fm.displayName(atPath: p)
                if name.hasSuffix(".app") { name.removeLast(4) }
                if name.isEmpty {
                    name = (p as NSString).lastPathComponent
                    if name.hasSuffix(".app") { name.removeLast(4) }
                }
                // Without a bundle ID: its path, as Steam's shortcuts to its
                // games, which have none.
                let id = b.bundleIdentifier ?? ""
                out.append(App(name: name, id: id.isEmpty ? p : id, path: p))
            }
        }
        return out
    }

    /// The list as apps get it (appsearch.Tidy): each app once (the first
    /// of an ID; the path where it has none), those without a name or a
    /// path left out, sorted by name (case and accents aside), then as
    /// written, then by path, so the same apps are always in the same
    /// order.
    nonisolated static func tidy(_ list: [App]) -> [App] {
        var out: [App] = []
        var seen = Set<String>()
        for a in list where !a.name.isEmpty && !a.path.isEmpty {
            let key = a.id.isEmpty ? a.path : a.id
            if seen.contains(key) { continue }
            seen.insert(key)
            out.append(a)
        }
        let keyed = out.enumerated().map { (AsukaAppSearch.fold($0.element.name), $0.offset, $0.element) }
        return keyed.sorted { x, y in
            if x.0 != y.0 { return x.0.unicodeScalars.lexicographicallyPrecedes(y.0.unicodeScalars) }
            if x.2.name != y.2.name { return x.2.name.unicodeScalars.lexicographicallyPrecedes(y.2.name.unicodeScalars) }
            if x.2.path != y.2.path { return x.2.path.unicodeScalars.lexicographicallyPrecedes(y.2.path.unicodeScalars) }
            return x.1 < y.1
        }.map(\.2)
    }

    /// Draws the icon Finder shows for an app (NSWorkspace's icon(forFile:),
    /// through a symbolic link: without its arrow) as a PNG in the caches,
    /// size points square at the main display's scale, and answers with its
    /// path. A file drawn since the app last changed is used again.
    static func icon(_ args: Data, _ reply: @escaping (Result<Data, Error>) -> Void) {
        guard let o = (try? JSONSerialization.jsonObject(with: args)) as? [String: Any],
              let path = o["path"] as? String, !path.isEmpty,
              let size = (o["size"] as? NSNumber)?.doubleValue, size > 0 else {
            return reply(.failure(AsukaModuleError("apps.icon: bad arguments")))
        }
        let scale = max(1, NSScreen.screens.first?.backingScaleFactor ?? 1)
        let px = Int((size * scale).rounded(.up))
        let cache = cacheDir()
        DispatchQueue.global(qos: .userInitiated).async {
            let fm = FileManager.default
            guard fm.fileExists(atPath: path) else { return reply(.failure(AsukaModuleError("no app at \(path)"))) }
            let real = (path as NSString).resolvingSymlinksInPath
            let out = iconFile(cache, path, px)
            if fresh(out, [real, real + "/Contents/Info.plist"]) { return reply(.success(Data(out.utf8))) }
            let img = NSWorkspace.shared.icon(forFile: real)
            guard let png = png(img, CGFloat(size), px) else {
                return reply(.failure(AsukaModuleError("couldn't draw the icon of \(path)")))
            }
            do {
                try write(out, png)
                reply(.success(Data(out.utf8)))
            } catch {
                reply(.failure(error))
            }
        }
    }

    /// The app's caches (asuka/fs's): ASUKA_CACHE, else Caches/<its ID>.
    static func cacheDir() -> String {
        if let d = ProcessInfo.processInfo.environment["ASUKA_CACHE"], !d.isEmpty { return d }
        let base = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first?.path ?? NSTemporaryDirectory()
        return (base as NSString).appendingPathComponent(AsukaRuntime.shared.appID)
    }

    /// Where an app's icon px pixels square is kept: a file of the app's
    /// caches named for its path.
    nonisolated static func iconFile(_ cache: String, _ path: String, _ px: Int) -> String {
        let sum = SHA256.hash(data: Data(path.utf8)).prefix(10).map { String(format: "%02x", $0) }.joined()
        return cache + "/asuka-apps/\(sum)-\(px).png"
    }

    /// Whether the icon at png was drawn after each of sources (the files
    /// it's drawn from) last changed.
    nonisolated static func fresh(_ png: String, _ sources: [String]) -> Bool {
        let fm = FileManager.default
        guard let t = (try? fm.attributesOfItem(atPath: png))?[.modificationDate] as? Date else { return false }
        for s in sources {
            if let st = (try? fm.attributesOfItem(atPath: s))?[.modificationDate] as? Date, st >= t { return false }
        }
        return true
    }

    /// Draws an image into an sRGB bitmap px pixels square that's pt points
    /// square (so the image picks its representation for that scale), as
    /// PNG; nil if it can't.
    nonisolated static func png(_ img: NSImage, _ pt: CGFloat, _ px: Int) -> Data? {
        guard var rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8, samplesPerPixel: 4,
                                         hasAlpha: true, isPlanar: false, colorSpaceName: .calibratedRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return nil }
        if let srgb = rep.retagging(with: .sRGB) { rep = srgb }
        rep.size = CGSize(width: pt, height: pt)
        guard let ctx = NSGraphicsContext(bitmapImageRep: rep) else { return nil }
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = ctx
        ctx.imageInterpolation = .high
        img.draw(in: CGRect(x: 0, y: 0, width: pt, height: pt))
        ctx.flushGraphics()
        NSGraphicsContext.restoreGraphicsState()
        return rep.representation(using: .png, properties: [:])
    }

    /// Writes an icon's PNG through a file beside it, renamed over it: a
    /// reader never sees half a file.
    nonisolated static func write(_ path: String, _ b: Data) throws {
        let dir = (path as NSString).deletingLastPathComponent
        try FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        let tmp = dir + "/.icon-" + UUID().uuidString
        try b.write(to: URL(fileURLWithPath: tmp))
        if rename(tmp, path) != 0 {
            unlink(tmp)
            throw AsukaModuleError("couldn't write \(path)")
        }
    }

    /// Starts the app at its path, or brings it to the front (NSWorkspace's
    /// openApplication, which answers off the main thread).
    static func open(_ path: String, _ reply: @escaping (Result<Data, Error>) -> Void) {
        var d: ObjCBool = false
        guard FileManager.default.fileExists(atPath: path, isDirectory: &d), d.boolValue else {
            return reply(.failure(AsukaModuleError("no app at \(path)")))
        }
        NSWorkspace.shared.openApplication(at: URL(fileURLWithPath: path), configuration: NSWorkspace.OpenConfiguration()) { _, err in
            DispatchQueue.main.async {
                if let err {
                    reply(.failure(AsukaModuleError("couldn't open \(path): " + asukaErrorText(err))))
                } else {
                    reply(.success(Data()))
                }
            }
        }
    }
}

/// Ranks names against what the user types, as a launcher does
/// (every desktop host answers apps.search alike).
enum AsukaAppSearch {
    // The ranks, best first.
    static let exact = 0, prefix = 1, word = 2, substring = 3, subsequence = 4

    /// The indices of the names that match query, best first: the name is
    /// the query; it starts with it; a word of it does (a word starts after
    /// a space or a mark, and at a capital in CamelCase), or the query is
    /// the starts of its words in order ("vsc": Visual Studio Code); the
    /// name has it; the name has its letters in order. Case, accents and
    /// width don't count. Within a rank the names are in order; those that
    /// match none are left out. An empty query is every index as it is.
    static func rank(_ names: [String], _ query: String) -> [Int] {
        let q = prepare(query)
        if q.text.isEmpty { return Array(names.indices) }
        let qc = q.compact
        var hits: [(Int, [Unicode.Scalar], Int)] = []
        for (i, n) in names.enumerated() {
            let r = rank(prepare(n), q.text, qc)
            if r >= 0 { hits.append((r, Array(fold(n).unicodeScalars), i)) }
        }
        hits.sort { a, b in
            if a.0 != b.0 { return a.0 < b.0 }
            if a.1 != b.1 { return a.1.lexicographicallyPrecedes(b.1) }
            let na = Array(names[a.2].unicodeScalars), nb = Array(names[b.2].unicodeScalars)
            if na != nb { return na.lexicographicallyPrecedes(nb) }
            return a.2 < b.2
        }
        return hits.map(\.2)
    }

    static func rank(_ name: Prepared, _ q: [Unicode.Scalar], _ qc: [Unicode.Scalar]) -> Int {
        let n = name.text
        if n == q { return exact }
        if hasPrefix(n[...], q) { return prefix }
        for i in n.indices where name.start[i] && hasPrefix(n[i...], q) { return word }
        if !qc.isEmpty && initials(name.words, qc) { return word }
        if index(n, q) >= 0 { return substring }
        if !qc.isEmpty && isSubsequence(n, qc) { return subsequence }
        return -1
    }

    /// Whether q is made of starts of words, in order: one start (a letter
    /// or more) per word, words skipped between them.
    static func initials(_ words: [[Unicode.Scalar]], _ q: [Unicode.Scalar]) -> Bool {
        var failed = Set<Int>()
        func match(_ w: Int, _ i: Int) -> Bool {
            if i == q.count { return true }
            if w == words.count || failed.contains(w << 32 | i) { return false }
            let word = words[w]
            var n = 0
            while n < word.count && i + n < q.count && word[n] == q[i + n] { n += 1 }
            var l = n
            while l > 0 {
                if match(w + 1, i + l) { return true }
                l -= 1
            }
            if match(w + 1, i) { return true }
            failed.insert(w << 32 | i)
            return false
        }
        return match(0, 0)
    }

    static func isSubsequence(_ s: [Unicode.Scalar], _ q: [Unicode.Scalar]) -> Bool {
        var j = 0
        for r in s where j < q.count && r == q[j] { j += 1 }
        return j == q.count
    }

    static func hasPrefix(_ s: ArraySlice<Unicode.Scalar>, _ p: [Unicode.Scalar]) -> Bool {
        s.count >= p.count && s.prefix(p.count).elementsEqual(p)
    }

    static func index(_ s: [Unicode.Scalar], _ q: [Unicode.Scalar]) -> Int {
        var i = 0
        while i + q.count <= s.count {
            if s[i..<i + q.count].elementsEqual(q) { return i }
            i += 1
        }
        return -1
    }

    /// A name folded for matching: its scalars, which of them start a word,
    /// and which are a word's (letters and digits) rather than what's
    /// between words.
    struct Prepared {
        var text: [Unicode.Scalar] = []
        var start: [Bool] = []
        var word: [Bool] = []

        /// The text without what's between words.
        var compact: [Unicode.Scalar] { text.indices.filter { word[$0] }.map { text[$0] } }

        /// The text's words: from each start to the next start or the next
        /// scalar that's not a word's.
        var words: [[Unicode.Scalar]] {
            var ws: [[Unicode.Scalar]] = []
            var i = 0
            while i < text.count {
                if !start[i] { i += 1; continue }
                var j = i + 1
                while j < text.count && word[j] && !start[j] { j += 1 }
                ws.append(Array(text[i..<j]))
                i = j
            }
            return ws
        }
    }

    // The kinds of scalars, for where words start.
    static let other = 0, upper = 1, lower = 2, digit = 3, mark = 4

    static func kind(_ r: Unicode.Scalar) -> Int {
        let p = r.properties
        switch p.generalCategory {
        case .nonspacingMark, .enclosingMark: return mark
        case .uppercaseLetter, .titlecaseLetter: return upper
        case .lowercaseLetter, .modifierLetter, .otherLetter: return lower
        case .decimalNumber, .letterNumber, .otherNumber: return digit
        default: return other
        }
    }

    /// Folds s scalar by scalar, marking where its words start: at its
    /// first letter or digit, after what's between words, at a capital
    /// after a small letter (TextEdit), at the last capital of a run before
    /// a small letter (XMLParser: Parser), and between letters and digits
    /// (Office365). Spaces run together into one; those at the ends go.
    static func prepare(_ s: String) -> Prepared {
        var p = Prepared()
        let rs = Array(s.unicodeScalars)
        var prev = other
        for (i, r) in rs.enumerated() {
            let k = kind(r)
            if k == mark { continue } // its letter's (folded away)
            var next = other
            for j in (i + 1)..<max(rs.count, i + 1) {
                let nk = kind(rs[j])
                if nk != mark { next = nk; break }
            }
            let isWord = k != other
            let start = isWord && (prev == other || prev == lower && k == upper || prev == upper && k == upper && next == lower || (prev == digit) != (k == digit))
            prev = k
            if !isWord && r.properties.isWhitespace {
                if p.text.isEmpty || p.text.last == " " { continue }
                p.text.append(" ")
                p.start.append(false)
                p.word.append(false)
                continue
            }
            var first = true
            for f in foldScalar(r) {
                p.text.append(f)
                p.start.append(start && first)
                p.word.append(isWord)
                first = false
            }
        }
        if p.text.last == " " {
            p.text.removeLast()
            p.start.removeLast()
            p.word.removeLast()
        }
        return p
    }

    /// s as search compares it: small letters, without accents (é is e),
    /// compatibility forms as their plain ones (ﬁ is fi, a full-width Ａ is
    /// a), and letters that don't decompose as the plain ones they're typed
    /// as (ß is ss, ø is o, æ is ae).
    static func fold(_ s: String) -> String {
        if s.utf8.allSatisfy({ $0 < 0x80 && !($0 >= 65 && $0 <= 90) }) { return s }
        var out = String.UnicodeScalarView()
        for r in s.unicodeScalars { out.append(contentsOf: foldScalar(r)) }
        return String(out)
    }

    static func lower(_ r: Unicode.Scalar) -> Unicode.Scalar {
        let l = r.properties.lowercaseMapping.unicodeScalars
        return l.count == 1 ? l.first! : r
    }

    /// r folded: no scalars for a combining mark.
    static func foldScalar(_ r: Unicode.Scalar) -> [Unicode.Scalar] {
        if r.value < 0x80 { return [lower(r)] }
        let r = lower(r)
        if let s = plain[r] { return Array(s.unicodeScalars) }
        var out: [Unicode.Scalar] = []
        for d in String(r).decomposedStringWithCompatibilityMapping.unicodeScalars {
            if d.properties.generalCategory == .nonspacingMark { continue }
            let d = lower(d)
            if let s = plain[d] { out.append(contentsOf: s.unicodeScalars); continue }
            out.append(d)
        }
        return out
    }

    /// The letters with no decomposition, as they're typed on a keyboard
    /// without them.
    static let plain: [Unicode.Scalar: String] = [
        "ß": "ss", "æ": "ae", "œ": "oe", "ø": "o", "đ": "d", "ð": "d",
        "ł": "l", "ħ": "h", "ı": "i", "þ": "th", "ŀ": "l", "ŧ": "t",
    ]
}
