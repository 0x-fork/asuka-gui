// platform/crash's native side on a Mac. The core keeps the run's record
// (asuka_crash_start: core/src/std/crash.c) and its own fatal errors; the
// host writes what only it sees in the record's inbox, for the core to
// read into reports:
//
// - a fatal signal (a bad access, an illegal instruction, a Swift trap,
//   an abort that isn't the core's panic), caught on a stack of its own
//   (sigaltstack: a stack overflow has none left) and written as the
//   app ends: inbox/exception-<UUID>.json, the format the iOS host writes
//   an uncaught exception in, with the crashed thread's return addresses
//   and the binaries they're in, for symbolicating;
// - MetricKit's diagnostics (macOS 12 and later: crashes of every kind,
//   hangs, CPU and disk exceptions), delivered on a later launch:
//   inbox/metrickit-<UUID>.json, the payload's JSON as it is.
//
// No handler of uncaught Objective-C exceptions: AppKit catches those on
// the main thread itself (and logs them), and MetricKit reports the ones
// that end the app. Nothing's installed if the app doesn't read its crash
// reports (asuka_crash_start answers "").
import AppKit
import Darwin
import MachO
#if canImport(CAsuka)
@_implementationOnly import CAsuka
#endif

/// Crash records: the core's (asuka_crash_start), and the Mac's crash
/// catching.
@MainActor enum AsukaCrash {
    private static var started = false

    /// Starts the run's record and the native handlers, as the host boots,
    /// before the app's init.
    static func start() {
        guard !started else { return }
        started = true
        // The record lives in the app's directory: the core needs it first.
        AsukaRuntime.shared.appDir.path.withCString { asuka_set_app_dir($0) }
        guard let p = infoJSON().withCString({ asuka_crash_start($0) }) else { return }
        let dir = String(cString: p)
        free(p)
        guard !dir.isEmpty else { return }
        let inbox = dir + "/inbox"
        try? FileManager.default.createDirectory(atPath: inbox, withIntermediateDirectories: true)
        AsukaSignalCrash.install(inbox: inbox, runs: dir + "/runs")
        AsukaMetrics.subscribe(inbox: inbox)
    }

    /// The Mac and the app, as JSON for asuka_crash_start: the model's
    /// identifier (MacBookPro18,3), macOS's version, the bundle's version
    /// and build, and whether it starts in front (not in the background).
    static func infoJSON() -> String {
        let info = Bundle.main.infoDictionary ?? [:]
        var version = sysctlString("kern.osproductversion")
        if version.isEmpty {
            let v = ProcessInfo.processInfo.operatingSystemVersion
            version = "\(v.majorVersion).\(v.minorVersion)" + (v.patchVersion > 0 ? ".\(v.patchVersion)" : "")
        }
        let cfg = AsukaRuntime.shared.config
        var d: [String: Any] = [
            "model": sysctlString("hw.model"),
            "system": "macOS",
            "systemVersion": version,
            "appVersion": info["CFBundleShortVersionString"] as? String ?? cfg.version,
            "build": info["CFBundleVersion"] as? String ?? cfg.build,
        ]
        d["foreground"] = !AsukaShell.shared.inBackground
        guard let b = try? JSONSerialization.data(withJSONObject: d, options: [.sortedKeys]) else { return "{}" }
        return String(decoding: b, as: UTF8.self)
    }

    static func sysctlString(_ name: String) -> String {
        var n = 0
        guard sysctlbyname(name, nil, &n, nil, 0) == 0, n > 0 else { return "" }
        var buf = [CChar](repeating: 0, count: n)
        guard sysctlbyname(name, &buf, &n, nil, 0) == 0 else { return "" }
        return String(cString: buf)
    }

    /// Writes b as a new file of the inbox, whole or not at all.
    nonisolated static func write(_ inbox: String, _ prefix: String, _ b: Data) {
        guard !b.isEmpty else { return }
        try? FileManager.default.createDirectory(atPath: inbox, withIntermediateDirectories: true)
        let path = inbox + "/" + prefix + UUID().uuidString + ".json"
        let tmp = path + ".tmp"
        guard FileManager.default.createFile(atPath: tmp, contents: b, attributes: [.posixPermissions: 0o600]) else { return }
        rename(tmp, path)
    }
}

// MARK: MetricKit

/// MetricKit's subscriber, found at run time: the framework is macOS 12's,
/// and the host runs on 11 (linking it would end the app there at launch).
/// Each diagnostic payload macOS delivers goes in the inbox, and the core
/// hears of it (asuka_crash_notify).
final class AsukaMetrics: NSObject {
    nonisolated(unsafe) static var inbox = ""
    nonisolated(unsafe) static var subscriber: AsukaMetrics?

    static func subscribe(inbox: String) {
        guard dlopen("/System/Library/Frameworks/MetricKit.framework/MetricKit", RTLD_LAZY) != nil,
              let cls = NSClassFromString("MXMetricManager") as? NSObject.Type,
              let proto = objc_getProtocol("MXMetricManagerSubscriber") else { return }
        Self.inbox = inbox
        class_addProtocol(AsukaMetrics.self, proto)
        let sub = AsukaMetrics()
        subscriber = sub // kept for the process's life
        guard let manager = cls.perform(NSSelectorFromString("sharedManager"))?.takeUnretainedValue() as? NSObject else { return }
        _ = manager.perform(NSSelectorFromString("addSubscriber:"), with: sub)
    }

    /// On a queue of MetricKit's.
    @objc(didReceiveDiagnosticPayloads:) func didReceiveDiagnosticPayloads(_ payloads: [NSObject]) {
        let sel = NSSelectorFromString("JSONRepresentation")
        for p in payloads where p.responds(to: sel) {
            if let d = p.perform(sel)?.takeUnretainedValue() as? Data {
                AsukaCrash.write(Self.inbox, "metrickit-", d)
            }
        }
        asuka_crash_notify()
    }
}

// MARK: Fatal signals

/// The fatal signals' handler. All it does as the app crashes is
/// async-signal-safe: it formats the report in memory it was given at
/// launch (no allocation), writes it with open, write and rename, then
/// puts the handler there was back and raises the signal again, so the
/// app ends as it would have (and macOS's own crash report is made). It
/// doesn't tell the core (asuka_crash_notify isn't safe in a signal
/// handler): the next launch reads the inbox.
enum AsukaSignalCrash {
    static let signals: [Int32] = [SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE, SIGTRAP]
    static let bufferSize = 64 * 1024
    static let maxFrames = 128

    /// The report's path and its temporary file's, as C strings.
    nonisolated(unsafe) static var path: UnsafeMutablePointer<CChar>?
    nonisolated(unsafe) static var tmp: UnsafeMutablePointer<CChar>?
    /// The run's .crash file, where the core writes its panic: an abort
    /// after one is the same crash, already recorded.
    nonisolated(unsafe) static var panicFile: UnsafeMutablePointer<CChar>?
    nonisolated(unsafe) static var run: UnsafeMutablePointer<CChar>?
    nonisolated(unsafe) static var buffer: UnsafeMutablePointer<UInt8>?
    nonisolated(unsafe) static var previous = UnsafeMutablePointer<sigaction>.allocate(capacity: 32)
    nonisolated(unsafe) static var crashing: Int32 = 0

    static func install(inbox: String, runs: String) {
        path = strdup(inbox + "/exception-" + UUID().uuidString + ".json")
        tmp = strdup(String(cString: path!) + ".tmp")
        buffer = .allocate(capacity: bufferSize)
        // This run's files are <start>-<pid>: the newest of this pid's.
        let pid = getpid()
        let names = (try? FileManager.default.contentsOfDirectory(atPath: runs)) ?? []
        let mine = names.filter { $0.hasSuffix("-\(pid).crash") }.sorted()
        if let last = mine.last {
            panicFile = strdup(runs + "/" + last)
            run = strdup(String(last.dropLast(".crash".count)))
        }
        // A stack of its own: a stack overflow leaves the thread's none.
        var ss = stack_t()
        let size = max(Int(SIGSTKSZ), 256 * 1024)
        ss.ss_sp = UnsafeMutableRawPointer.allocate(byteCount: size, alignment: 16)
        ss.ss_size = size
        ss.ss_flags = 0
        sigaltstack(&ss, nil)
        for s in signals {
            var a = sigaction()
            a.__sigaction_u.__sa_sigaction = asukaCrashSignal
            a.sa_flags = SA_SIGINFO | SA_ONSTACK
            sigemptyset(&a.sa_mask)
            sigaction(s, &a, previous + Int(s))
        }
    }
}

/// The report's JSON, written into a fixed buffer.
private struct AsukaCrashWriter {
    let p: UnsafeMutablePointer<UInt8>
    let cap: Int
    var n = 0

    mutating func byte(_ b: UInt8) {
        if n < cap { p[n] = b; n += 1 }
    }
    mutating func raw(_ s: StaticString) {
        s.withUTF8Buffer { for b in $0 { byte(b) } }
    }
    mutating func cstr(_ s: UnsafePointer<CChar>?) {
        guard var q = s else { return }
        while q.pointee != 0 {
            let c = UInt8(bitPattern: q.pointee)
            if c == UInt8(ascii: "\"") || c == UInt8(ascii: "\\") { byte(UInt8(ascii: "\\")) }
            if c >= 0x20 { byte(c) }
            q += 1
        }
    }
    mutating func uint(_ v: UInt64) {
        var digits: (UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8,
                     UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8) =
            (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        var k = 0
        var x = v
        withUnsafeMutableBytes(of: &digits) { d in
            repeat {
                d[k] = UInt8(ascii: "0") + UInt8(x % 10)
                x /= 10
                k += 1
            } while x > 0
            while k > 0 {
                k -= 1
                byte(d[k])
            }
        }
    }
    mutating func int(_ v: Int64) {
        if v < 0 { byte(UInt8(ascii: "-")); uint(UInt64(-(v + 1)) + 1) } else { uint(UInt64(v)) }
    }
    mutating func hex(_ v: UInt64) {
        raw("0x")
        var started = false
        for i in stride(from: 60, through: 0, by: -4) {
            let d = UInt8((v >> UInt64(i)) & 0xf)
            if d != 0 || started || i == 0 {
                started = true
                byte(d < 10 ? UInt8(ascii: "0") + d : UInt8(ascii: "a") + d - 10)
            }
        }
    }
    mutating func hexByte(_ b: UInt8) {
        let digits: StaticString = "0123456789ABCDEF"
        digits.withUTF8Buffer { d in
            byte(d[Int(b >> 4)])
            byte(d[Int(b & 0xf)])
        }
    }
}

/// The name of a fatal signal.
private func asukaSignalName(_ s: Int32) -> StaticString {
    switch s {
    case SIGSEGV: return "SIGSEGV"
    case SIGBUS: return "SIGBUS"
    case SIGILL: return "SIGILL"
    case SIGABRT: return "SIGABRT"
    case SIGFPE: return "SIGFPE"
    case SIGTRAP: return "SIGTRAP"
    default: return "signal"
    }
}

/// The image (binary) holding address a: its header, name and __TEXT's
/// range, read from dyld's list without taking its lock.
private func asukaImage(_ a: UInt) -> (header: UnsafePointer<mach_header>, name: UnsafePointer<CChar>?)? {
    let count = _dyld_image_count()
    for i in 0..<count {
        guard let h = _dyld_get_image_header(i), h.pointee.magic == MH_MAGIC_64 else { continue }
        let slide = UInt(bitPattern: _dyld_get_image_vmaddr_slide(i))
        var cmd = UnsafeRawPointer(h) + MemoryLayout<mach_header_64>.size
        for _ in 0..<h.pointee.ncmds {
            let lc = cmd.assumingMemoryBound(to: load_command.self).pointee
            if lc.cmd == UInt32(LC_SEGMENT_64) {
                let seg = cmd.assumingMemoryBound(to: segment_command_64.self)
                let isText = withUnsafeBytes(of: seg.pointee.segname) { n in
                    n[0] == 0x5f && n[1] == 0x5f && n[2] == 0x54 && n[3] == 0x45 && n[4] == 0x58 && n[5] == 0x54 && n[6] == 0
                }
                if isText {
                    let start = UInt(seg.pointee.vmaddr) &+ slide
                    if a >= start && a < start &+ UInt(seg.pointee.vmsize) { return (h, _dyld_get_image_name(i)) }
                    break
                }
            }
            cmd += Int(lc.cmdsize)
        }
    }
    return nil
}

/// Writes the image's UUID (its LC_UUID, as dSYMs and MetricKit say it).
private func asukaImageUUID(_ h: UnsafePointer<mach_header>, _ w: inout AsukaCrashWriter) -> Bool {
    var cmd = UnsafeRawPointer(h) + MemoryLayout<mach_header_64>.size
    for _ in 0..<h.pointee.ncmds {
        let lc = cmd.assumingMemoryBound(to: load_command.self).pointee
        if lc.cmd == UInt32(LC_UUID) {
            var u = cmd.assumingMemoryBound(to: uuid_command.self).pointee.uuid
            withUnsafeBytes(of: &u) { b in
                for i in 0..<16 {
                    if i == 4 || i == 6 || i == 8 || i == 10 { w.byte(UInt8(ascii: "-")) }
                    w.hexByte(b[i])
                }
            }
            return true
        }
        cmd += Int(lc.cmdsize)
    }
    return false
}

private func asukaFrame(_ a: UInt, _ w: inout AsukaCrashWriter) {
    w.raw("{\"address\":")
    w.uint(UInt64(a))
    if let img = asukaImage(a) {
        w.raw(",\"offset\":")
        w.uint(UInt64(a - UInt(bitPattern: img.header)))
        if let name = img.name {
            // Its file's name, without the folders.
            var last = name
            var q = name
            while q.pointee != 0 {
                if q.pointee == 0x2f { last = q + 1 }
                q += 1
            }
            w.raw(",\"binary\":\"")
            w.cstr(last)
            w.raw("\"")
        }
        let mark = w.n
        w.raw(",\"binaryId\":\"")
        if asukaImageUUID(img.header, &w) { w.raw("\"") } else { w.n = mark }
    }
    w.raw("}")
}

private func asukaAddFrame(_ a: UInt, _ w: inout AsukaCrashWriter, _ count: inout Int) {
    guard a != 0, count < AsukaSignalCrash.maxFrames else { return }
    if count > 0 { w.byte(UInt8(ascii: ",")) }
    asukaFrame(a, &w)
    count += 1
}

/// The handler: the report, then the crash goes on as it would have.
private func asukaCrashSignal(_ sig: Int32, _ info: UnsafeMutablePointer<__siginfo>?, _ uap: UnsafeMutableRawPointer?) {
    // One report, from the first thread that crashes.
    if OSAtomicCompareAndSwap32(0, 1, &AsukaSignalCrash.crashing) {
        asukaWriteCrash(sig, info, uap)
    }
    sigaction(sig, AsukaSignalCrash.previous + Int(sig), nil)
    // A fault happens again as the handler returns; a signal sent (kill,
    // abort) is raised again, delivered then.
    raise(sig)
}

private func asukaWriteCrash(_ sig: Int32, _ info: UnsafeMutablePointer<__siginfo>?, _ uap: UnsafeMutableRawPointer?) {
    guard let buf = AsukaSignalCrash.buffer, let path = AsukaSignalCrash.path, let tmp = AsukaSignalCrash.tmp else { return }
    if sig == SIGABRT, let pf = AsukaSignalCrash.panicFile {
        // The core's panic ends with abort, its own report written.
        // A descriptor's size: open and lseek are async-signal-safe.
        let fd = open(pf, O_RDONLY)
        if fd >= 0 {
            let size = lseek(fd, 0, SEEK_END)
            close(fd)
            if size > 0 { return }
        }
    }
    var w = AsukaCrashWriter(p: buf, cap: AsukaSignalCrash.bufferSize)
    var ts = timespec()
    clock_gettime(CLOCK_REALTIME, &ts)
    w.raw("{\"time\":")
    w.int(Int64(ts.tv_sec) * 1000 + Int64(ts.tv_nsec) / 1_000_000)
    w.raw(",\"pid\":")
    w.int(Int64(getpid()))
    if let run = AsukaSignalCrash.run {
        w.raw(",\"run\":\"")
        w.cstr(run)
        w.raw("\"")
    }
    w.raw(pthread_main_np() != 0 ? ",\"thread\":\"main\"" : ",\"thread\":\"Thread\"")
    w.raw(",\"name\":\"")
    w.raw(asukaSignalName(sig))
    w.raw("\",\"reason\":\"")
    if let i = info {
        w.raw("code ")
        w.int(Int64(i.pointee.si_code))
        if sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE {
            w.raw(" at ")
            w.hex(UInt64(UInt(bitPattern: i.pointee.si_addr)))
        }
    }
    w.raw("\",\"stack\":\"\",\"fatal\":true,\"frames\":[")
    // The crashed thread's stack, by its frame pointers, from where it
    // was when the signal came (its saved registers).
    var pc: UInt = 0, fp: UInt = 0, lr: UInt = 0
    if let u = uap?.assumingMemoryBound(to: ucontext_t.self), let mc = u.pointee.uc_mcontext {
        #if arch(arm64)
        pc = UInt(mc.pointee.__ss.__pc)
        fp = UInt(mc.pointee.__ss.__fp)
        lr = UInt(mc.pointee.__ss.__lr)
        #elseif arch(x86_64)
        pc = UInt(mc.pointee.__ss.__rip)
        fp = UInt(mc.pointee.__ss.__rbp)
        #endif
    }
    let me = pthread_self()
    let top = UInt(bitPattern: pthread_get_stackaddr_np(me))
    let bottom = top &- UInt(pthread_get_stacksize_np(me))
    var count = 0
    asukaAddFrame(pc, &w, &count)
    #if arch(arm64)
    // A leaf function's caller is in the link register, not yet on the
    // stack.
    if lr != 0 && lr != pc { asukaAddFrame(lr & 0x0000_ffff_ffff_ffff, &w, &count) }
    #endif
    while fp >= bottom && fp < top && fp & 7 == 0 && count < AsukaSignalCrash.maxFrames {
        let frame = UnsafePointer<UInt>(bitPattern: fp)!
        let next = frame[0]
        var ret = frame[1]
        #if arch(arm64)
        ret &= 0x0000_ffff_ffff_ffff // pointer authentication's bits
        #endif
        if ret == 0 { break }
        if !(count == 2 && ret == (lr & 0x0000_ffff_ffff_ffff)) { asukaAddFrame(ret, &w, &count) }
        if next <= fp { break }
        fp = next
    }
    w.raw("]}")
    let fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0o600)
    guard fd >= 0 else { return }
    var off = 0
    while off < w.n {
        let k = Darwin.write(fd, buf + off, w.n - off)
        if k <= 0 { break }
        off += k
    }
    close(fd)
    rename(tmp, path)
}
