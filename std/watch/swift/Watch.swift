// asuka/watch on macOS: FSEvents. A watch keeps what it saw of the files
// and, a moment after FSEvents says something changed, looks again and
// compares: the events are the differences, a batch at a time (as the
// Windows and Linux C does: desktop/watch.c). A file written many times in
// a row is one write; a file replaced at its path (an editor's atomic
// save) is a write; a file that moved within the directory is a rename
// with its old path (the same inode).
//
// The stream is the directory's (a file's: its folder's), with file
// events and its root watched; FSEvents covers the tree under it, so the
// directories below need no stream of their own. Its callbacks and the
// looking again run on the watch's serial queue.
import Foundation
import CoreServices
import Asuka

func makeWatching() throws -> any Watching { MacWatching() }

func makeWatcher() throws -> any Watcher {
    throw AsukaNativeError.unsupported("a watcher comes from Watching.open")
}

final class MacWatching: Watching {
    func `open`(path: String, ignore: [String], recursive: Bool, attributes: Bool) async throws -> any Watcher {
        // Listing a large tree takes a while: off the main thread.
        try await withCheckedThrowingContinuation { (k: CheckedContinuation<any Watcher, Error>) in
            DispatchQueue.global(qos: .userInitiated).async {
                k.resume(with: Result { try MacWatcher(path: path, ignore: ignore, recursive: recursive, attributes: attributes) })
            }
        }
    }
}

/// An entry of the snapshot, by its path relative to the root, with
/// slashes ("." the root itself).
struct WatchEntry {
    var dir: Bool
    var size: UInt64
    var mtime: UInt64
    var id: UInt64 // the inode, for renames
    var mode: UInt32
}

/// How long the watch waits for changes to stop, and the longest it waits
/// from the first (ms).
private let debounce: UInt64 = 50, maxDelay: UInt64 = 500

final class MacWatcher: Watcher {
    let rootPath: String
    let rootDir: Bool
    let recursive: Bool
    let attributes: Bool
    let ignore: [String]
    private let queue = DispatchQueue(label: "asuka.watch")
    private var stream: FSEventStreamRef?
    private var snap: [String: WatchEntry] = [:]

    // On the queue.
    private var dirty = false, overflow = false
    private var first: UInt64 = 0, last: UInt64 = 0
    private var settling = false
    private var stopped = false, ended = false
    private var changed: (([Event]) -> Void)?
    private var following: CheckedContinuation<Void, Error>?
    private var pending: [[Event]] = []

    init(path: String, ignore: [String], recursive: Bool, attributes: Bool) throws {
        guard let real = realpath(path, nil) else {
            throw NSError(domain: "watch", code: 0, userInfo: [NSLocalizedDescriptionKey: "watch: no file or directory at \(path)"])
        }
        rootPath = String(cString: real)
        free(real)
        var st = stat()
        guard stat(rootPath, &st) == 0 else {
            throw NSError(domain: "watch", code: 0, userInfo: [NSLocalizedDescriptionKey: "watch: no file or directory at \(path)"])
        }
        rootDir = st.st_mode & S_IFMT == S_IFDIR
        self.recursive = recursive
        self.attributes = attributes
        self.ignore = ignore
        guard let s = scan() else {
            throw NSError(domain: "watch", code: 0, userInfo: [NSLocalizedDescriptionKey: "watch: \(rootPath) went"])
        }
        snap = s
        try start()
    }

    deinit {
        if let s = stream {
            FSEventStreamStop(s)
            FSEventStreamInvalidate(s)
            FSEventStreamRelease(s)
        }
    }

    // MARK: Watcher

    func root() throws -> String { rootPath }

    func follow(changed: @escaping ([Event]) -> Void) async throws {
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (k: CheckedContinuation<Void, Error>) in
                queue.async {
                    if self.following != nil {
                        k.resume(throwing: NSError(domain: "watch", code: 0, userInfo: [NSLocalizedDescriptionKey: "the watch is followed already"]))
                        return
                    }
                    if self.ended {
                        k.resume() // closed, or its directory went
                        return
                    }
                    self.changed = changed
                    self.following = k
                    let batches = self.pending
                    self.pending = []
                    for b in batches { changed(b) }
                }
            }
        } onCancel: {
            queue.async {
                guard let k = self.following else { return }
                self.following = nil
                self.changed = nil
                k.resume(throwing: CancellationError())
            }
        }
    }

    func close() throws {
        queue.async { self.stop() }
    }

    // MARK: The stream

    private func start() throws {
        // A file, through its directory: replaced at its path, it's written.
        let dir = rootDir ? rootPath : (rootPath as NSString).deletingLastPathComponent
        var ctx = FSEventStreamContext(version: 0, info: Unmanaged.passUnretained(self).toOpaque(), retain: nil, release: nil, copyDescription: nil)
        let flags = FSEventStreamCreateFlags(kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagWatchRoot | kFSEventStreamCreateFlagNoDefer)
        let callback: FSEventStreamCallback = { _, info, count, _, eventFlags, _ in
            guard let info else { return }
            let w = Unmanaged<MacWatcher>.fromOpaque(info).takeUnretainedValue()
            let dropped = FSEventStreamEventFlags(kFSEventStreamEventFlagMustScanSubDirs | kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagKernelDropped)
            var overflow = false
            for i in 0..<count where eventFlags[i] & dropped != 0 { overflow = true }
            w.signal(overflow)
        }
        guard let s = FSEventStreamCreate(nil, callback, &ctx, [dir] as CFArray, FSEventStreamEventId(kFSEventStreamEventIdSinceNow), 0, flags) else {
            throw NSError(domain: "watch", code: 0, userInfo: [NSLocalizedDescriptionKey: "watch: FSEvents can't watch \(dir)"])
        }
        FSEventStreamSetDispatchQueue(s, queue)
        guard FSEventStreamStart(s) else {
            FSEventStreamInvalidate(s)
            FSEventStreamRelease(s)
            throw NSError(domain: "watch", code: 0, userInfo: [NSLocalizedDescriptionKey: "watch: FSEvents can't watch \(dir)"])
        }
        stream = s
    }

    /// Something changed (on the queue): looked at again once the changes
    /// settle.
    private func signal(_ dropped: Bool) {
        guard !stopped else { return }
        let now = Self.nowMS()
        if !dirty { first = now }
        dirty = true
        last = now
        overflow = overflow || dropped
        scheduleSettle()
    }

    private func scheduleSettle() {
        guard dirty, !settling else { return }
        settling = true
        let due = min(last + debounce, first + maxDelay)
        let now = Self.nowMS()
        queue.asyncAfter(deadline: .now() + .milliseconds(Int(due > now ? due - now : 0))) {
            self.settling = false
            self.settle()
        }
    }

    /// Looks again when the changes have settled, and delivers the
    /// differences; the root gone, a remove of "." ends the watch.
    private func settle() {
        guard dirty, !stopped else { return }
        let now = Self.nowMS()
        let due = min(last + debounce, first + maxDelay)
        if due > now {
            scheduleSettle()
            return
        }
        let dropped = overflow
        dirty = false
        overflow = false
        guard let fresh = scan() else {
            deliver([Event(op: .remove, path: ".", oldPath: "", isDir: rootDir)])
            stop()
            return
        }
        var b = diff(snap, fresh)
        if dropped {
            // FSEvents dropped changes: the watch looked at everything.
            b.insert(Event(op: .overflow, path: ".", oldPath: "", isDir: rootDir), at: 0)
        }
        snap = fresh
        deliver(b)
    }

    /// Sends a batch to follow, or keeps it for follow (on the queue).
    private func deliver(_ b: [Event]) {
        guard !b.isEmpty else { return }
        if let c = changed { c(b) } else { pending.append(b) }
    }

    /// Ends the watch (on the queue): its stream stops, and follow returns.
    private func stop() {
        guard !stopped else { return }
        stopped = true
        ended = true
        if let s = stream {
            FSEventStreamStop(s)
            FSEventStreamInvalidate(s)
            FSEventStreamRelease(s)
            stream = nil
        }
        changed = nil
        if let k = following {
            following = nil
            k.resume()
        }
    }

    static func nowMS() -> UInt64 { DispatchTime.now().uptimeNanoseconds / 1_000_000 }

    // MARK: The snapshot

    private func entry(_ st: stat) -> WatchEntry {
        let mtime = UInt64(st.st_mtimespec.tv_sec) * 1_000_000_000 + UInt64(st.st_mtimespec.tv_nsec)
        return WatchEntry(dir: st.st_mode & S_IFMT == S_IFDIR, size: UInt64(st.st_size), mtime: mtime,
                          id: UInt64(st.st_ino) ^ UInt64(UInt32(bitPattern: st.st_dev)) << 48, mode: UInt32(st.st_mode & 0o7777))
    }

    /// The snapshot of the root (a directory, or a file as "."), with
    /// recursive the directories under it too; nil when the root isn't
    /// there (or isn't what it was).
    func scan() -> [String: WatchEntry]? {
        var st = stat()
        guard lstat(rootPath, &st) == 0, (st.st_mode & S_IFMT == S_IFDIR) == rootDir else { return nil }
        var out: [String: WatchEntry] = [".": entry(st)]
        if rootDir { walk(rootPath, ".", &out) }
        return out
    }

    private func walk(_ path: String, _ rel: String, _ out: inout [String: WatchEntry]) {
        guard let names = try? FileManager.default.contentsOfDirectory(atPath: path) else { return }
        for name in names {
            let p = path + "/" + name
            let r = rel == "." ? name : rel + "/" + name
            var st = stat()
            guard lstat(p, &st) == 0 else { continue }
            let isDir = st.st_mode & S_IFMT == S_IFDIR
            guard !watchIgnored(ignore, r, isDir) else { continue }
            out[r] = entry(st)
            if isDir && recursive { walk(p, r, &out) } // not through symlinks: lstat
        }
    }

    // MARK: Differences

    /// The events from old to now: removes and creates (what's in a
    /// directory that came or went comes or goes with it), renames (the
    /// same inode), writes, and with attributes permission changes.
    func diff(_ old: [String: WatchEntry], _ now: [String: WatchEntry]) -> [Event] {
        func parent(_ rel: String) -> String? {
            guard let i = rel.lastIndex(of: "/") else { return nil }
            return String(rel[..<i])
        }
        func top(_ rels: [String], _ inSetOf: [String: WatchEntry], _ notIn: [String: WatchEntry]) -> [String] {
            rels.filter { r in
                guard let p = parent(r) else { return true }
                return inSetOf[p] == nil || notIn[p] != nil
            }
        }
        let gone = top(old.keys.filter { now[$0] == nil }.sorted(), old, now)
        let came = top(now.keys.filter { old[$0] == nil }.sorted(), now, old)
        var out: [Event] = []
        var paired = Array(repeating: false, count: came.count)
        for g in gone {
            let ge = old[g]!
            var to = -1
            if ge.id != 0 {
                for (k, c) in came.enumerated() where !paired[k] && now[c]!.id == ge.id && now[c]!.dir == ge.dir {
                    to = k
                    break
                }
            }
            if to < 0 {
                out.append(Event(op: .remove, path: g, oldPath: "", isDir: ge.dir))
            } else {
                paired[to] = true
                out.append(Event(op: .rename, path: came[to], oldPath: g, isDir: ge.dir))
            }
        }
        for (k, c) in came.enumerated() where !paired[k] {
            out.append(Event(op: .create, path: c, oldPath: "", isDir: now[c]!.dir))
        }
        for r in now.keys.sorted() {
            guard let n = now[r], let o = old[r] else { continue }
            if !n.dir && (n.size != o.size || n.mtime != o.mtime || (n.id != 0 && o.id != 0 && n.id != o.id)) {
                out.append(Event(op: .write, path: r, oldPath: "", isDir: false))
            } else if attributes && n.mode != o.mode {
                out.append(Event(op: .attrib, path: r, oldPath: "", isDir: n.dir))
            }
        }
        return out
    }
}

// MARK: Patterns: a .gitignore's lines

/// Whether the patterns ignore rel ("." never).
func watchIgnored(_ patterns: [String], _ rel: String, _ dir: Bool) -> Bool {
    if rel == "." { return false }
    let base = rel.split(separator: "/").last.map(String.init) ?? rel
    for raw in patterns {
        var p = raw
        let dirOnly = p.hasSuffix("/")
        if dirOnly { p.removeLast() }
        if dirOnly && !dir { continue }
        let anyName = !p.contains("/")
        if p.hasPrefix("/") { p.removeFirst() }
        if p.isEmpty { continue }
        if anyName {
            if globMatch(Array(p.utf8), 0, Array(base.utf8), 0) { return true }
        } else {
            let pat = p.split(separator: "/", omittingEmptySubsequences: false).map(String.init)
            let parts = rel.split(separator: "/", omittingEmptySubsequences: false).map(String.init)
            if partsMatch(pat[...], parts[...]) { return true }
        }
    }
    return false
}

/// Whether the parts of a path match those of a pattern, where "**"
/// matches any number of them.
private func partsMatch(_ pat: ArraySlice<String>, _ parts: ArraySlice<String>) -> Bool {
    guard let p0 = pat.first else { return parts.isEmpty }
    if p0 == "**" {
        if pat.count == 1 { return !parts.isEmpty } // "logs/**": what is in logs
        for i in 0...parts.count where partsMatch(pat.dropFirst(), parts.dropFirst(i)) { return true }
        return false
    }
    guard let s0 = parts.first else { return false }
    return globMatch(Array(p0.utf8), 0, Array(s0.utf8), 0) && partsMatch(pat.dropFirst(), parts.dropFirst())
}

/// path.Match's: *, ?, [a-z] (^ negates), \ escapes; no '/'.
private func globMatch(_ p: [UInt8], _ pi: Int, _ s: [UInt8], _ si: Int) -> Bool {
    var pi = pi, si = si
    let slash = UInt8(ascii: "/")
    while pi < p.count {
        switch p[pi] {
        case UInt8(ascii: "*"):
            while pi + 1 < p.count && p[pi + 1] == UInt8(ascii: "*") { pi += 1 }
            if pi + 1 == p.count { return !s[si...].contains(slash) }
            var t = si
            while t < s.count && s[t] != slash {
                if globMatch(p, pi + 1, s, t + 1) || globMatch(p, pi + 1, s, t) { return true }
                t += 1
            }
            return globMatch(p, pi + 1, s, si)
        case UInt8(ascii: "?"):
            if si >= s.count || s[si] == slash { return false }
            si += 1
        case UInt8(ascii: "["):
            if si >= s.count { return false }
            var q = pi + 1
            let neg = q < p.count && p[q] == UInt8(ascii: "^")
            if neg { q += 1 }
            var ok = false
            while q < p.count && p[q] != UInt8(ascii: "]") {
                var lo = p[q]
                if lo == UInt8(ascii: "\\") && q + 1 < p.count { q += 1; lo = p[q] }
                var hi = lo
                if q + 2 < p.count && p[q + 1] == UInt8(ascii: "-") && p[q + 2] != UInt8(ascii: "]") {
                    q += 2
                    hi = p[q]
                    if hi == UInt8(ascii: "\\") && q + 1 < p.count { q += 1; hi = p[q] }
                }
                if s[si] >= lo && s[si] <= hi { ok = true }
                q += 1
            }
            if q >= p.count || ok == neg { return false }
            pi = q
            si += 1
        case UInt8(ascii: "\\"):
            if pi + 1 < p.count { pi += 1 }
            fallthrough
        default:
            if si >= s.count || s[si] != p[pi] { return false }
            si += 1
        }
        pi += 1
    }
    return si == s.count
}
