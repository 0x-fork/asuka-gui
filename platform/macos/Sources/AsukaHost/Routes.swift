// A window's Router address comes back as the window does (host/darwin's
// routes.go, internal/routestate): the last Location command each window
// was sent is kept, by the window's key (its WindowOptions' StateKey,
// "main" for the main window unless it names another; a Window's
// StateKey, none without one), in asuka-routes.json in the app's
// directory, and given back as its Router mounts. Written a moment after
// it changes, off the main thread, so the main thread never waits for the
// disk; and at once as the app goes to the background or quits.
//
// The file is internal/routestate's, read by every desktop host: a JSON
// object of the windows' keys, each {"href": ..., "state": ...} (the
// state left out when it's empty), kept by the C they share (platform/
// desktop's state.c: dsk_routes).
//
// The C core sends no window key (its Router's commands leave it empty)
// and asks no host for the address at mount: the host keys the command by
// the window it's sent to, and, the first time a window's Router tells
// its address in a run, gives the kept one back as the Location event
// (what a browser's back sends), which the Router navigates to.
import AppKit
@_implementationOnly import CAsuka
#if canImport(AsukaOps)
import AsukaOps
#endif

@MainActor final class AsukaRoutes {
    static let shared = AsukaRoutes()

    /// The file's name in the app's directory.
    static let fileName = "asuka-routes.json"
    /// How long a change waits before it's written: navigating quickly
    /// writes once.
    static let delay = 0.3

    struct Entry: Equatable {
        var href: String
        var state: [UInt8]
    }

    /// The file ("": keep none).
    let path: String
    /// The store (dsk_routes: its lock lets a flush run off the main
    /// thread).
    private nonisolated(unsafe) let store: OpaquePointer?
    private var pending = false
    /// The windows whose kept address was given back (or that had none to
    /// give) this run.
    private var restored: Set<String> = []
    /// Writes happen one at a time, in order, off the main thread.
    private let disk = DispatchQueue(label: "asuka.routes")

    convenience init() { self.init(path: AsukaRoutes.defaultPath()) }

    init(path: String) {
        self.path = path
        store = path.isEmpty ? dsk_routes_open(nil) : dsk_routes_open(path)
    }

    /// The file's path: none in tests, automation runs and under asuka dev,
    /// whose reloads keep the router's state themselves (ASUKA_ROUTES=1
    /// keeps them there too, for the host's tests).
    static func defaultPath() -> String {
        let env = ProcessInfo.processInfo.environment
        if env["ASUKA_ROUTES"] != "1" {
            for k in ["ASUKA_SNAPSHOT", "ASUKA_SCRIPT", "ASUKA_TEST", "ASUKA_STATE", "ASUKA_DEV"] where !(env[k] ?? "").isEmpty {
                return ""
            }
        }
        return AsukaRuntime.shared.appDir.appendingPathComponent(fileName).path
    }

    /// Where the window keyed key was left.
    func entry(_ key: String) -> Entry? {
        guard !key.isEmpty, let store else { return nil }
        var href: UnsafePointer<CChar>?
        var state: UnsafePointer<CChar>?
        var n = 0
        guard dsk_routes_location(store, key, &href, &state, &n), let href else { return nil }
        let bytes = state.map { p in p.withMemoryRebound(to: UInt8.self, capacity: n) { Array(UnsafeBufferPointer(start: $0, count: n)) } } ?? []
        return Entry(href: String(cString: href), state: bytes)
    }

    /// The Location command (ops.DecodeLocation: u8 action, i32 delta, a
    /// u32-length href, state and window key), from the window's root or
    /// Window node n.
    func location(_ b: [UInt8], from n: AsukaNode? = nil) {
        var r = AsukaByteReader(b)
        let action = r.u8()
        _ = r.u32() // delta
        let href = r.string()
        let state = r.bytes()
        var key = r.string()
        guard !r.bad else { return }
        if key.isEmpty, let n { key = windowKey(n) }
        guard !key.isEmpty else { return }
        if !restored.contains(key) {
            restored.insert(key)
            // The Router has just mounted (its first address, a replace):
            // it goes where the window was left.
            if let kept = entry(key), let n, !kept.href.isEmpty, action == UInt8(AsukaLocationAction.replace.rawValue), kept.href != href {
                var w = AsukaByteWriter()
                w.string(kept.href)
                w.data(kept.state)
                let ev = w.bytes
                AsukaRuntime.shared.emit(n, .location) { AsukaEvents.location($0, ev) }
                return
            }
        }
        set(key, href, state)
    }

    /// The key n's window keeps its state under.
    func windowKey(_ n: AsukaNode) -> String {
        let rt = AsukaRuntime.shared
        if let m = rt.main, n === rt.root || n === m.root {
            return m.cfg.stateKey.isEmpty ? "main" : m.cfg.stateKey
        }
        for w in rt.wins.values where w.root === n { return w.cfg.stateKey }
        return ""
    }

    /// Keeps the window keyed key's address, written a moment later.
    func set(_ key: String, _ href: String, _ state: [UInt8]) {
        guard !key.isEmpty, let store else { return }
        // Never a null pointer, even empty: the store compares them.
        let s = state.map { CChar(bitPattern: $0) } + [0]
        let changed = href.withCString { h in
            s.withUnsafeBufferPointer { dsk_routes_set(store, key, h, strlen(h), $0.baseAddress, state.count) }
        }
        guard changed, !path.isEmpty, !pending else { return }
        pending = true
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.delay) { [weak self] in
            MainActor.assumeIsolated { self?.write(wait: false) }
        }
    }

    /// Writes what changed now (the app goes to the background, quits).
    func flush() { write(wait: true) }

    private func write(wait: Bool) {
        guard pending, !path.isEmpty, let store else { return }
        pending = false
        let job = { dsk_routes_flush(store) }
        if wait { disk.sync(execute: job) } else { disk.async(execute: job) }
    }
}
