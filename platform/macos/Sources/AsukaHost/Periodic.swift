// asuka/periodic on a Mac: NSBackgroundActivityScheduler, which runs a task
// about every interval while the app runs (in the background too: a Mac app
// keeps running out of sight), when the system has the time and power for
// it; unlike iOS's BGTaskScheduler it doesn't launch the app. "schedule"
// (string name, u32 seconds, u8 flags: 1 network, 2 charging) and "cancel"
// (the name) keep the app's tasks (in UserDefaults, as iOS's host does) and
// a scheduler for each; when one fires, "next" hands the app the task
// (string name, u32 seconds it has), which it reports with "done" (string
// name, u8 ok). A task that wants power waits while the Mac runs on its
// battery, one that wants the network while it has none (deferred: the
// scheduler tries again soon).
import AppKit
import IOKit.ps
import Network

@MainActor final class AsukaPeriodic {
    static let shared = AsukaPeriodic()
    static let key = "asuka.periodic"

    struct Task: Codable {
        var every: Double
        var network: Bool
        var charging: Bool
        var last: Double
    }

    typealias Completion = (NSBackgroundActivityScheduler.Result) -> Void

    private var loaded = false
    private(set) var schedulers: [String: NSBackgroundActivityScheduler] = [:]
    private var due: [String] = []
    private var waiting: [AsukaReply] = []
    /// A task's name: the scheduler's completion handler, while it runs.
    private var running: [String: Completion] = [:]

    var tasks: [String: Task] {
        get {
            guard let d = UserDefaults.standard.data(forKey: Self.key) else { return [:] }
            return (try? JSONDecoder().decode([String: Task].self, from: d)) ?? [:]
        }
        set {
            if let b = try? JSONEncoder().encode(newValue) { UserDefaults.standard.set(b, forKey: Self.key) }
        }
    }

    func methods() -> [String: AsukaModuleMethod] {
        [
            "schedule": { [unowned self] args, reply in
                var r = AsukaByteReader(args)
                let name = r.string()
                let secs = r.u32()
                let flags = r.u8()
                if name.isEmpty || r.bad { return reply(.failure(AsukaModuleError("bad args"))) }
                var ts = self.tasks
                var last = ts[name]?.last ?? 0
                if last == 0 { last = Date().timeIntervalSince1970.rounded(.down) }
                ts[name] = Task(every: Double(secs), network: flags & 1 != 0, charging: flags & 2 != 0, last: last)
                self.tasks = ts
                self.load()
                self.schedule(name, ts[name]!)
                reply(.success(Data()))
            },
            "cancel": { [unowned self] args, reply in
                let name = String(decoding: args, as: UTF8.self)
                var ts = self.tasks
                ts.removeValue(forKey: name)
                self.tasks = ts
                self.unschedule(name)
                reply(.success(Data()))
            },
            "next": { [unowned self] _, reply in
                self.load()
                if !self.due.isEmpty {
                    let name = self.due.removeFirst()
                    return reply(.success(self.run(name)))
                }
                self.waiting.append(reply)
            },
            "done": { [unowned self] args, reply in
                var r = AsukaByteReader(args)
                let name = r.string()
                let ok = r.u8() == 1
                self.finished(name, ok)
                reply(.success(Data()))
            },
        ]
    }

    /// Schedules the tasks kept from the app's last runs, once.
    private func load() {
        if loaded { return }
        loaded = true
        for (name, t) in tasks { schedule(name, t) }
    }

    /// "next"'s answer: the name and the seconds it has.
    private func run(_ name: String) -> Data {
        var w = AsukaByteWriter()
        w.string(name)
        w.u32(tasks[name]?.charging == true ? 240 : 25)
        return w.data
    }

    private func schedule(_ name: String, _ t: Task) {
        unschedule(name)
        let id = Bundle.main.bundleIdentifier ?? "dev.asuka." + AsukaRuntime.shared.appName
        let sch = NSBackgroundActivityScheduler(identifier: id + ".periodic." + name)
        sch.repeats = true
        sch.interval = max(t.every, 60)
        sch.tolerance = max(t.every / 4, 30)
        sch.qualityOfService = .utility
        schedulers[name] = sch
        // Called on a queue of its own: the task goes to the main thread.
        sch.schedule { completion in
            DispatchQueue.main.async { MainActor.assumeIsolated { AsukaPeriodic.shared.fire(name, completion) } }
        }
    }

    private func unschedule(_ name: String) {
        if let sch = schedulers.removeValue(forKey: name) { sch.invalidate() }
        if let i = due.firstIndex(of: name) { due.remove(at: i) }
        running.removeValue(forKey: name)?(.finished)
    }

    /// A scheduler's turn: the task goes to the app, unless what it wants
    /// isn't there now.
    func fire(_ name: String, _ done: @escaping Completion) {
        guard let t = tasks[name] else { return done(.finished) }
        if t.charging && !Self.onAC() || t.network && !online { return done(.deferred) }
        if running[name] != nil { return done(.deferred) } // still running from the last turn
        running[name] = done
        if !waiting.isEmpty {
            waiting.removeFirst()(.success(run(name)))
            return
        }
        due.append(name)
    }

    private func finished(_ name: String, _ ok: Bool) {
        guard let done = running.removeValue(forKey: name) else { return }
        if ok {
            var ts = tasks
            if var t = ts[name] {
                t.last = Date().timeIntervalSince1970.rounded(.down)
                ts[name] = t
                tasks = ts
            }
        }
        done(.finished)
    }

    // MARK: What a task waits for

    private var monitor: NWPathMonitor?
    /// The network path's state, once the monitor has said it.
    private var connected: Bool?

    /// Whether the Mac has a network path (the monitor, started for it:
    /// unknown yet counts as online).
    var online: Bool {
        if monitor == nil {
            let m = NWPathMonitor()
            m.pathUpdateHandler = { p in
                let ok = p.status == .satisfied
                DispatchQueue.main.async { MainActor.assumeIsolated { AsukaPeriodic.shared.connected = ok } }
            }
            m.start(queue: DispatchQueue(label: "asuka.periodic.network"))
            monitor = m
        }
        return connected ?? true
    }

    /// Whether the Mac runs on its power adapter (a desktop Mac always
    /// does).
    static func onAC() -> Bool {
        guard let blob = IOPSCopyPowerSourcesInfo()?.takeRetainedValue() else { return true }
        let t = (IOPSGetProvidingPowerSourceType(blob)?.takeUnretainedValue() as String?) ?? ""
        return t.isEmpty || t == kIOPSACPowerValue
    }
}
