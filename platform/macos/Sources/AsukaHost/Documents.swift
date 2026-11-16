// The module that hands files to the system's composers: asuka/compose
// (NSSharingService, mailto:), and the files it names. Preview (Quick
// Look) and print modules aren't here: asuka/preview and asuka/print have
// their own Swift.
import AppKit
import UniformTypeIdentifiers

/// A file a module call names: its source and MIME type.
struct AsukaSource: Decodable {
    var source = ""
    var type = ""
    var name = ""

    init(source: String, type: String = "") {
        self.source = source
        self.type = type
    }

    init(from d: Decoder) throws {
        let c = try d.container(keyedBy: CodingKeys.self)
        source = (try? c.decodeIfPresent(String.self, forKey: .source)) ?? ""
        type = (try? c.decodeIfPresent(String.self, forKey: .type)) ?? ""
        name = (try? c.decodeIfPresent(String.self, forKey: .name)) ?? ""
    }

    enum CodingKeys: String, CodingKey { case source, type, name }
}

/// The file a source names, for the modules that hand
/// files to the system: a path, a file:// URL, an asset's path, or an
/// http(s) URL, downloaded first.
enum AsukaLocalFile {
    /// The file source names: under assets for an asset's path; a download
    /// into a new folder in cache (its name the server's, or the URL's,
    /// with the extension of the type it gives if it has none) for an
    /// http(s) URL. Not on the main thread: a download waits.
    static func resolve(_ source: String, _ type: String, assets: URL, cache: URL) throws -> String {
        var p = source
        if source.hasPrefix("http://") || source.hasPrefix("https://") {
            return try download(source, type, cache)
        } else if source.hasPrefix("file://") {
            guard let u = URL(string: source) else { throw AsukaModuleError("no file at \(source)") }
            p = u.path
        } else if !source.hasPrefix("/") {
            p = assets.appendingPathComponent(source).path
        }
        var dir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: p, isDirectory: &dir), !dir.boolValue else {
            throw AsukaModuleError("no file at \(source)")
        }
        return p
    }

    static func download(_ source: String, _ type: String, _ cache: URL) throws -> String {
        guard let url = URL(string: source) else { throw AsukaModuleError("couldn't download \(source)") }
        let sem = DispatchSemaphore(value: 0)
        var result: (URL?, URLResponse?, Error?)
        let task = URLSession.shared.downloadTask(with: url) { tmp, resp, err in
            // The file goes when the handler returns: moved aside now.
            var kept: URL?
            if let tmp {
                let k = FileManager.default.temporaryDirectory.appendingPathComponent("asuka-dl-" + asukaRandomName())
                if (try? FileManager.default.moveItem(at: tmp, to: k)) != nil { kept = k }
            }
            result = (kept, resp, err)
            sem.signal()
        }
        task.resume()
        sem.wait()
        let (tmp, resp, err) = result
        guard let tmp else { throw err ?? AsukaModuleError("couldn't download \(source)") }
        if let h = resp as? HTTPURLResponse, h.statusCode / 100 != 2 {
            try? FileManager.default.removeItem(at: tmp)
            throw AsukaModuleError("couldn't download \(source): \(h.statusCode) \(HTTPURLResponse.localizedString(forStatusCode: h.statusCode))")
        }
        var name = resp?.suggestedFilename ?? ""
        if let h = resp as? HTTPURLResponse, h.value(forHTTPHeaderField: "Content-Disposition") == nil { name = "" }
        if name.isEmpty { name = url.lastPathComponent }
        name = name.trimmingCharacters(in: .whitespaces)
            .replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "\\", with: "_").replacingOccurrences(of: ":", with: "_")
        if name.isEmpty || name == "." || name == "_" || name == "/" { name = "file" }
        if (name as NSString).pathExtension.isEmpty {
            var t = type
            if t.isEmpty { t = resp?.mimeType ?? "" }
            if let ext = UTType(mimeType: t.trimmingCharacters(in: .whitespaces))?.preferredFilenameExtension { name += "." + ext }
        }
        let dir = cache.appendingPathComponent("asuka-files", isDirectory: true).appendingPathComponent(String(asukaRandomName().prefix(16)), isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let dst = dir.appendingPathComponent(name)
        try FileManager.default.moveItem(at: tmp, to: dst)
        return dst.path
    }
}

@MainActor extension AsukaRuntime {
    func registerDocumentModules() {
        var compose: [String: AsukaModuleMethod] = [:]
        for m in ["can", "mail", "message"] {
            compose[m] = { [unowned self] args, reply in self.compose(m, args, reply) }
        }
        registerModule("compose", compose)
    }

    /// Finds the files sources name (downloading URLs), off the main
    /// thread, and calls done with them on it.
    func localFiles(_ sources: [AsukaSource], _ done: @escaping @MainActor (Result<[String], Error>) -> Void) {
        let assets = self.assets, cache = asukaCacheDir
        DispatchQueue.global(qos: .userInitiated).async {
            let r = Result { try sources.map { try AsukaLocalFile.resolve($0.source, $0.type, assets: assets, cache: cache) } }
            DispatchQueue.main.async { MainActor.assumeIsolated { done(r) } }
        }
    }

    // MARK: Compose

    struct ComposeArgs: Decodable {
        var to: [String] = [], cc: [String] = [], bcc: [String] = []
        var subject = "", body = "", text = "", mailto = ""
        var html = false
        var attachments: [AsukaSource] = []

        init(from d: Decoder) throws {
            let c = try d.container(keyedBy: CodingKeys.self)
            func s(_ k: CodingKeys) -> String { (try? c.decodeIfPresent(String.self, forKey: k)) ?? "" }
            func l(_ k: CodingKeys) -> [String] { ((try? c.decodeIfPresent([String].self, forKey: k)) ?? nil) ?? [] }
            to = l(.to); cc = l(.cc); bcc = l(.bcc)
            subject = s(.subject); body = s(.body); text = s(.text); mailto = s(.mailto)
            html = (try? c.decodeIfPresent(Bool.self, forKey: .html)) ?? false
            attachments = ((try? c.decodeIfPresent([AsukaSource].self, forKey: .attachments)) ?? nil) ?? []
        }

        enum CodingKeys: String, CodingKey { case to, cc, bcc, subject, body, text, mailto, html, attachments }
    }

    /// Composing: mailto: links, and the share services for mail with
    /// attachments and for messages.
    func compose(_ method: String, _ args: Data, _ reply: @escaping AsukaReply) {
        if method == "can" {
            let mail = URL(string: "mailto:a@example.com").flatMap { NSWorkspace.shared.urlForApplication(toOpen: $0) } != nil
            let text = NSSharingService(named: .composeMessage)?.canPerform(withItems: [""]) ?? false // a text to send
            reply(.success(Data([mail ? 1 : 0, 0, text ? 1 : 0, text ? 1 : 0])))
            return
        }
        guard let a = try? JSONDecoder().decode(ComposeArgs.self, from: args) else {
            return reply(.failure(AsukaModuleError("compose.\(method): bad arguments")))
        }
        if method == "mail" && a.attachments.isEmpty {
            // The user's mail app, with all the fields the link has.
            guard let u = URL(string: a.mailto), NSWorkspace.shared.open(u) else { return reply(.failure(AsukaModuleError("unavailable"))) }
            return reply(.success(Data([3])))
        }
        if method == "mail" && a.cc.count + a.bcc.count > 0 {
            return reply(.failure(AsukaModuleError("unavailable"))) // the share service has no CC or BCC
        }
        let name: NSSharingService.Name
        switch method {
        case "mail": name = .composeEmail
        case "message": name = .composeMessage
        default: return reply(.failure(AsukaModuleError("asuka: compose.\(method) isn't on macOS")))
        }
        localFiles(a.attachments) { r in
            guard case .success(let paths) = r else {
                if case .failure(let e) = r { reply(.failure(e)) }
                return
            }
            guard let s = NSSharingService(named: name) else { return reply(.failure(AsukaModuleError("unavailable"))) }
            var items: [Any] = []
            var body = a.body
            if method == "message" && !a.subject.isEmpty { body = (a.subject + "\n" + a.body).trimmingCharacters(in: .whitespacesAndNewlines) }
            if a.html && !body.isEmpty {
                if let s = NSAttributedString(html: Data(body.utf8), documentAttributes: nil) { items.append(s) } else { items.append(a.text) }
            } else if !body.isEmpty {
                items.append(body)
            }
            for (i, p) in paths.enumerated() { items.append(URL(fileURLWithPath: asukaNamed(p, a.attachments[i].name))) }
            guard s.canPerform(withItems: items) else { return reply(.failure(AsukaModuleError("unavailable"))) }
            s.recipients = a.to
            if method == "mail" { s.subject = a.subject }
            AsukaComposer.shared.perform(s, items, reply)
        }
    }
}

/// The share services composing, and their replies.
@MainActor final class AsukaComposer: NSObject, NSSharingServiceDelegate {
    static let shared = AsukaComposer()
    private var replies: [ObjectIdentifier: (NSSharingService, AsukaReply)] = [:]

    func perform(_ s: NSSharingService, _ items: [Any], _ reply: @escaping AsukaReply) {
        s.delegate = self
        replies[ObjectIdentifier(s)] = (s, reply)
        s.perform(withItems: items)
    }

    func sharingService(_ s: NSSharingService, didShareItems items: [Any]) {
        replies.removeValue(forKey: ObjectIdentifier(s))?.1(.success(Data([3])))
    }

    func sharingService(_ s: NSSharingService, didFailToShareItems items: [Any], error: Error) {
        guard let (_, r) = replies.removeValue(forKey: ObjectIdentifier(s)) else { return }
        if (error as NSError).code == NSUserCancelledError { r(.success(Data([0]))) } else { r(.failure(AsukaModuleError(error.localizedDescription))) }
    }
}

/// path under name (a link to it in a folder of its own in the caches) if
/// its name isn't name, for the attachment's name in the mail.
@MainActor func asukaNamed(_ path: String, _ name: String) -> String {
    let name = name.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: ":", with: "_")
    if name.isEmpty || name == (path as NSString).lastPathComponent { return path }
    let fm = FileManager.default
    let dir = asukaCacheDir.appendingPathComponent("asuka-files", isDirectory: true).appendingPathComponent("named-" + asukaRandomName(), isDirectory: true)
    guard (try? fm.createDirectory(at: dir, withIntermediateDirectories: true)) != nil else { return path }
    let dst = dir.appendingPathComponent(name).path
    if (try? fm.linkItem(atPath: path, toPath: dst)) == nil {
        guard (try? fm.copyItem(atPath: path, toPath: dst)) != nil else { return path }
    }
    return dst
}
