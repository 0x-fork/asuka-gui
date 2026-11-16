// A Mac app's entry point: the host runs the app (compiled into the core,
// or under asuka dev the bytecode asuka dev sends) until it quits.
import AppKit

@main
enum AsukaMain {
    static func main() {
        MainActor.assumeIsolated { AsukaRuntime.main() }
    }
}
