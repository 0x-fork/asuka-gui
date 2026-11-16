#if canImport(UIKit)
import UIKit

/// A native package's code that runs as the app launches, before the
/// app's own code: its manifest's `ios.launch` names the class, by its
/// Objective-C name (`@objc(AsukaLocationLaunch)`), which the CLI lists
/// in Info.plist's AsukaLaunch, so only the apps that import the package
/// (or use the member whose `[uses]` names it) run it. What the system
/// wants made before launch ends goes here: Core Location's delegate for
/// a relaunch to cross a region, Core Bluetooth's managers with their
/// restore identifiers, StoreKit's transaction listener.
@objc public protocol AsukaLaunching: NSObjectProtocol {
    /// Runs on the main thread, with the launch's options. It answers
    /// whether the system launched the app for the package (a region, a
    /// restored Bluetooth connection): then the app's code starts at once,
    /// without a screen (a scene that comes later shows it), so it reads
    /// what woke the app, which the package keeps until it does.
    @MainActor static func launch(_ options: [UIApplication.LaunchOptionsKey: Any]) -> Bool
}

/// Runs the app's packages' launch code (AsukaLaunching), from Info.plist's
/// AsukaLaunch.
@MainActor enum AsukaLaunch {
    static func run(_ options: [UIApplication.LaunchOptionsKey: Any]) {
        guard let names = Bundle.main.object(forInfoDictionaryKey: "AsukaLaunch") as? [String] else { return }
        var woke = false
        for name in names {
            guard let c = NSClassFromString(name) as? AsukaLaunching.Type else {
                NSLog("asuka: no launch class %@ (a package's ios.launch)", name)
                continue
            }
            if c.launch(options) { woke = true }
        }
        if woke && UIApplication.shared.applicationState == .background {
            AsukaRuntime.shared.startHeadless()
        }
    }
}
#endif
