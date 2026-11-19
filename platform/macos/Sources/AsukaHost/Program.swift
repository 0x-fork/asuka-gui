// A program's entry (docs/programs.md) when its packages have Swift
// (std/http's URLSession): no application and no window, the core's
// program run on the main queue's loop, the packages' modules answering
// as they do in an app. The program exits as its main ends.
import Foundation
#if canImport(CAsuka)
@_implementationOnly import CAsuka
#endif

public enum AsukaProgram {
    /// Runs the program compiled into this build. From main.
    public static func main() -> Never {
        AsukaNative.done = { call, b in b.withUnsafeBufferPointer { asuka_native_done(call, $0.baseAddress, $0.count) } }
        AsukaNative.invoke = { id, b in
            var out: UnsafeMutablePointer<UInt8>? = nil
            let n = b.withUnsafeBufferPointer { asuka_native_invoke(id, $0.baseAddress, $0.count, &out) }
            guard let out else { return nil }
            defer { asuka_free(out) }
            return Array(UnsafeBufferPointer(start: out, count: n))
        }
        var cb = asuka_host_callbacks(
            apply: { _, _ in },
            measure: { _, _, _, _, _, ow, oh in
                ow?.pointee = 0
                oh?.pointee = 0
            },
            post: { DispatchQueue.main.async { asuka_run_posted() } },
            native_new: { cls, len, iface, out, cap, big in
                let name = String(decoding: UnsafeBufferPointer(start: cls, count: len), as: UTF8.self)
                return AsukaNative.write(AsukaNative.new(name, iface), out, cap, big)
            },
            native_call: { h, method, p, n, out, cap, big in
                AsukaNative.write(AsukaNative.call(h, method, Array(UnsafeBufferPointer(start: p, count: n))), out, cap, big)
            },
            native_start: { h, method, p, n, call in
                AsukaNative.start(h, method, Array(UnsafeBufferPointer(start: p, count: n)), call)
            },
            native_cancel: { AsukaNative.cancel($0) },
            native_release: { AsukaNative.release($0) },
            native_free: { free($0) },
            measure_v: nil
        )
        let code = asuka_program_start(CommandLine.argc, CommandLine.unsafeArgv, &cb)
        if code >= 0 { exit(code) }
        dispatchMain()
    }
}
