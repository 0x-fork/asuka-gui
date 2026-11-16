// STUB (part: nav). Navigation and windows: Stack, Screen, Tabs, SplitView,
// Sheet, Alert, Popover, Snackbar, Window; toolbars, search, back; menus
// (a button's, a view's context menu); transitions and matched views;
// window options (WindowOptions, a Window's Config), actions and state;
// panels and flyouts. Ports host/darwin's nav.go, window.go (openWindow),
// winctl.go, flyout.go, split.go, popover.go, snackbar.go, transition.go,
// match.go, commands.go's menus. The nav agent replaces this file (and may
// split it into more nav files), keeping these functions' signatures.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

/// What navigation keeps of a node (stacks, screens, tabs, sheets, alerts,
/// windows).
@MainActor final class AsukaNav {
    /// Stack: Replace's, until its next insert: changes happen at once.
    var instant = false
    /// Whether the host sizes n (a screen, a sheet's content), not the core.
    func sizedByHost(_ n: AsukaNode) -> Bool { false }
}

/// What a window's title and toolbar show.
@MainActor final class AsukaChrome {}
/// A window's options' and state's (winctl, flyouts).
@MainActor final class AsukaWinShell {}
@MainActor final class AsukaSnack {}
@MainActor final class AsukaPopover {}
@MainActor final class AsukaSplit {}

@MainActor extension AsukaRuntime {
    func createNav(_ n: AsukaNode) -> Bool { false }
    func destroyNav(_ n: AsukaNode) -> Bool { false }
    func insertNav(_ p: AsukaNode, _ c: AsukaNode, _ i: Int) -> Bool { false }
    func removeNav(_ p: AsukaNode, _ c: AsukaNode) -> Bool { false }
    func insertSplit(_ p: AsukaNode, _ c: AsukaNode, _ i: Int) -> Bool { false }
    func setNavCommon(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func setNavProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func makeSplit(_ n: AsukaNode) -> NSView { AsukaView(frame: .zero) }
    func setSplitProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func layoutScreens(_ n: AsukaNode) {}
    /// Sizes every host-sized view again (a window resized).
    func layoutNav() {}
    func updateWindowChrome() {}
    func present(_ n: AsukaNode) {}
    func back(_ w: AsukaWin) {}
    /// Escape in a view (a sheet's content dismisses it).
    func cancel(_ v: NSView) {}
    func tapTab(_ title: String) -> Bool { false }
    func tapSidebar(_ title: String) -> Bool { false }
    func tapToolbar(_ label: String) -> Bool { false }
    /// A toolbar item's action (sender: its item or its view).
    func barItem(_ sender: Any?) {}
    /// A view's Menu prop changed: its NSMenu made again (n.menu, byMenu).
    func setMenu(_ n: AsukaNode) {}
    /// A button's Tap menu.
    func popUp(_ n: AsukaNode) {}
    func alertFieldChanged(_ f: NSTextField) -> Bool { false }
    func alertFieldCommand(_ c: NSControl, _ cmd: Selector) -> Bool? { nil }
    func searchChanged(_ f: NSTextField) {}
    func searchCommand(_ c: NSControl, _ cmd: Selector) -> Bool { false }
    func snackPressed(_ n: AsukaNode, _ sender: Any?) {}
    // Transitions and matched views.
    func matchOut(_ n: AsukaNode) -> Bool { false }
    func exit(_ n: AsukaNode) -> Bool { false }
    func inExiting(_ v: NSView?) -> Bool { false }
    func matchIn(_ n: AsukaNode) {}
    func setMatch(_ n: AsukaNode, _ key: String) {}
    func enterWithTransition(_ n: AsukaNode, _ r: CGRect) { n.view?.frame = r }
    // Windows.
    /// A Window's content changed size: its environment reported.
    func windowContentResized(_ w: AsukaWin) {}
    func windowOptions(_ b: [UInt8]) {}
    func windowAction(_ n: AsukaNode, _ b: [UInt8]) {}
    func setWindowTitle(_ s: String) {}
    func listenWindow(_ n: AsukaNode) {}
    /// Reports the windows' states that changed (WindowState), once a turn.
    func windowsChanged() {}
    /// The room a hidden title bar's strip takes over w's content.
    func safeTop(_ w: AsukaWin) -> Float { 0 }
    /// Escape in a flyout's or a panel's field closes it.
    func escapeFloating(_ w: NSWindow) -> Bool { false }
    /// After the core started the app: the main window configured (its
    /// options, or the host's) and shown, unless it opens hidden or the app
    /// runs in the background.
    func startWindows() { main.window.makeKeyAndOrderFront(nil) }

    // Hooks (Parts.swift).
    func navDestroyed(_ n: AsukaNode) {}
    func navPlaced(_ n: AsukaNode) {}
    func navDirection(_ n: AsukaNode, _ changed: Bool) {}
    func navBatchApplied(_ frames: Bool) {}
    func navJSON(_ n: AsukaNode, _ d: inout [String: Any]) {}
    func navWindowJSON(_ w: AsukaWin, _ d: inout [String: Any]) {}
    func registerNavModules() {}
}
