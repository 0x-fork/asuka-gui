// platform/appearance on iOS and the Mac. iOS has no accent of the system's
// (an app's tint is its own); Increase Contrast and Reduce Transparency
// are UIAccessibility's. The Mac's accent is NSColor's
// controlAccentColor (multicolor is blue), in sRGB, and its settings
// NSWorkspace's accessibility display options.
import Foundation
#if canImport(UIKit)
import UIKit
#else
import AppKit
#endif

final class SystemAppearance: System {
    func current() throws -> Settings {
        if Thread.isMainThread { return MainActor.assumeIsolated { Self.settings() } }
        return DispatchQueue.main.sync { MainActor.assumeIsolated { Self.settings() } }
    }

    func watch(changed: @escaping (Settings) -> Void) async throws {
        let w = await MainActor.run { AppearanceWatch(changed) }
        defer { Task { @MainActor in w.stop() } }
        while !Task.isCancelled {
            try? await Task.sleep(nanoseconds: 3_600_000_000_000)
        }
    }

    @MainActor static func settings() -> Settings {
        #if canImport(UIKit)
        return Settings(increaseContrast: UIAccessibility.isDarkerSystemColorsEnabled, reduceTransparency: UIAccessibility.isReduceTransparencyEnabled)
        #else
        var s = Settings()
        if let c = NSColor.controlAccentColor.usingColorSpace(.sRGB) {
            func ch(_ v: CGFloat) -> Int { Int((min(max(v, 0), 1) * 255).rounded()) }
            s.hasAccent = true
            s.red = ch(c.redComponent)
            s.green = ch(c.greenComponent)
            s.blue = ch(c.blueComponent)
        }
        let ws = NSWorkspace.shared
        s.increaseContrast = ws.accessibilityDisplayShouldIncreaseContrast
        s.reduceTransparency = ws.accessibilityDisplayShouldReduceTransparency
        return s
        #endif
    }
}

/// Follows the settings' notifications, reporting the settings when they
/// change (and at once).
@MainActor final class AppearanceWatch: NSObject {
    private let changed: (Settings) -> Void
    private var last: Settings?

    init(_ changed: @escaping (Settings) -> Void) {
        self.changed = changed
        super.init()
        #if canImport(UIKit)
        let nc = NotificationCenter.default
        for n in [UIAccessibility.darkerSystemColorsStatusDidChangeNotification, UIAccessibility.reduceTransparencyStatusDidChangeNotification] {
            nc.addObserver(self, selector: #selector(report), name: n, object: nil)
        }
        #else
        NotificationCenter.default.addObserver(self, selector: #selector(report), name: NSColor.systemColorsDidChangeNotification, object: nil)
        NSWorkspace.shared.notificationCenter.addObserver(self, selector: #selector(report), name: NSWorkspace.accessibilityDisplayOptionsDidChangeNotification, object: nil)
        #endif
        report()
    }

    @objc func report() {
        let s = SystemAppearance.settings()
        if let l = last, l.hasAccent == s.hasAccent, l.red == s.red, l.green == s.green, l.blue == s.blue,
           l.increaseContrast == s.increaseContrast, l.reduceTransparency == s.reduceTransparency { return }
        last = s
        changed(s)
    }

    func stop() {
        NotificationCenter.default.removeObserver(self)
        #if os(macOS)
        NSWorkspace.shared.notificationCenter.removeObserver(self)
        #endif
    }
}

func makeSystem() throws -> any System { SystemAppearance() }
