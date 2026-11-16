// Flyouts, panels, windows sized to their content and click-through
// windows (WindowFlyout, WindowPanel, WindowFitContent,
// WindowClickThrough).
//
// A flyout and a panel are AsukaPanels: NSPanels with the nonactivating
// style from the start (the window server learns that a window never
// activates its app only from the style it's made with), borderless, a
// clear background under the system's material (NSVisualEffectView: a
// menu's for a flyout, a popover's for a panel) unless the app gives a
// background, rounded corners and the window's shadow. They can be the key
// window, never the main one: the app the user is in stays active, its
// menu bar showing. A panel joins every Space, as an auxiliary of apps in
// full screen, out of Mission Control and the window cycle, at the
// floating level; without a place of the app's it shows on the display
// with the pointer, centered across it, a third of the way down. A flyout
// hanging from a tray icon is at the level of menus, placed against its
// status item's button; one hanging from a rectangle of its parent is the
// parent's child window. Both close as the user closes them (CloseRequest,
// or Dismissed; the main window's closing) on an Escape their content
// leaves, as they stop being the key window, and on a click in another app
// (a global monitor: a nonactivating panel's app isn't active, so another
// app's activating doesn't always take the key window from it), unless a
// sheet or a modal Window of theirs shows: a modal Window over a flyout or
// a panel is its child window at its level, centered over it, not a sheet
// hanging from it, and gives it the key back as it closes.
//
// A window sized to its content takes WindowAction Fit's size, keeping its
// top left (a flyout is placed again); one that opens shown shows once it
// has, so it never shows at another size first. Click-through is
// ignoresMouseEvents.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

/// The window of flyouts and panels.
final class AsukaPanel: NSPanel {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { false }

    /// NSPanel closes on an Escape (cancelOperation:) past the core: the
    /// content's views have their say first, then the user's closing.
    override func cancelOperation(_ sender: Any?) {
        guard let v = contentView else { return }
        MainActor.assumeIsolated { AsukaRuntime.shared.cancel(v) }
    }
}

/// A rectangle, y down (a flyout's placement: internal/flyout).
struct AsukaFlyoutRect: Equatable {
    var x: CGFloat = 0, y: CGFloat = 0, w: CGFloat = 0, h: CGFloat = 0

    var right: CGFloat { x + w }
    var bottom: CGFloat { y + h }

    func contains(_ px: CGFloat, _ py: CGFloat) -> Bool { px >= x && py >= y && px < right && py < bottom }
}

/// Places a flyout window next to its anchor, as the desktop hosts all do
/// (internal/flyout): on the side its Placement names, flipped to the other
/// side where that has no room and the other has, lined up with the anchor
/// along that side (flipped to the other end where that has no room), then
/// slid back into the work area of its display, then shrunk to fit it, as
/// GTK places menus and Wayland compositors place popups. Coordinates are
/// points, y down; start is left, as frames are physical.
enum AsukaFlyoutPlacement {
    /// A w by h rectangle centered in r, its top a third of the way down
    /// the room left (as Spotlight's panel shows), within r.
    static func center(_ w: CGFloat, _ h: CGFloat, _ r: AsukaFlyoutRect) -> AsukaFlyoutRect {
        let w = min(w, r.w), h = min(h, r.h)
        return AsukaFlyoutRect(x: r.x + (r.w - w) / 2, y: r.y + (r.h - h) / 3, w: w, h: h)
    }

    /// The frame of a w by h flyout next to anchor, gap away, as placement
    /// says, within work (the work area of the display the anchor is on).
    static func place(_ anchor: AsukaFlyoutRect, _ w: CGFloat, _ h: CGFloat, _ placement: Int32, _ gap: CGFloat, _ work: AsukaFlyoutRect) -> AsukaFlyoutRect {
        var p = placement
        if p < 0 || p > AsukaPlacement.leftEnd.rawValue { p = AsukaPlacement.bottom.rawValue }
        // Its side (bottom, top, right, left) and alignment (center, start,
        // end).
        var side = p / 3
        let align = p % 3
        var r = AsukaFlyoutRect(w: w, h: h)
        if side == 0 || side == 1 {
            let after = work.bottom - (anchor.bottom + gap)
            let before = anchor.y - gap - work.y
            if side == 0 && h > after && before > after { side = 1 } else if side == 1 && h > before && after > before { side = 0 }
            r.y = side == 0 ? anchor.bottom + gap : anchor.y - gap - h
            r.x = lineUp(anchor.x, anchor.w, w, align, work.x, work.right)
        } else {
            let after = work.right - (anchor.right + gap)
            let before = anchor.x - gap - work.x
            if side == 2 && w > after && before > after { side = 3 } else if side == 3 && w > before && after > before { side = 2 }
            r.x = side == 2 ? anchor.right + gap : anchor.x - gap - w
            r.y = lineUp(anchor.y, anchor.h, h, align, work.y, work.bottom)
        }
        // Slid back into the work area, then shrunk to it.
        r.w = min(r.w, work.w)
        r.h = min(r.h, work.h)
        r.x = max(min(r.x, work.right - r.w), work.x)
        r.y = max(min(r.y, work.bottom - r.h), work.y)
        return r
    }

    /// Where a flyout size long starts along an anchor at pos, n long,
    /// lined up as align says; an end with no room flips to the other end
    /// where that has room.
    static func lineUp(_ pos: CGFloat, _ n: CGFloat, _ size: CGFloat, _ align: Int32, _ lo: CGFloat, _ hi: CGFloat) -> CGFloat {
        switch align {
        case 1: // start
            if pos + size > hi && pos + n - size >= lo { return pos + n - size }
            return pos
        case 2: // end
            if pos + n - size < lo && pos + size <= hi { return pos }
            return pos + n - size
        default:
            return pos + (n - size) / 2
        }
    }
}

/// r, in AppKit's screen coordinates (y up from the main display's
/// bottom), in Asuka's (y down from its top).
@MainActor func asukaYDown(_ r: CGRect) -> AsukaFlyoutRect {
    AsukaFlyoutRect(x: r.minX, y: asukaMainScreenHeight() - r.minY - r.height, w: r.width, h: r.height)
}

/// r back in AppKit's coordinates.
@MainActor func asukaYUp(_ r: AsukaFlyoutRect) -> CGRect {
    CGRect(x: r.x, y: asukaMainScreenHeight() - r.y - r.h, width: r.w, height: r.h)
}

@MainActor extension AsukaRuntime {
    /// A flyout's or a panel's style mask: borderless and nonactivating, a
    /// panel resizable unless it's fixed.
    func floatingStyle(_ c: AsukaWindowConfig) -> NSWindow.StyleMask {
        var m: NSWindow.StyleMask = [.borderless, .nonactivatingPanel]
        if c.flags.contains(.panel) && !c.flags.contains(.fixedSize) { m.insert(.resizable) }
        return m
    }

    /// An AsukaPanel of content size, with c's style.
    func newPanelWindow(_ size: CGSize, _ c: AsukaWindowConfig) -> NSWindow {
        AsukaPanel(contentRect: CGRect(origin: .zero, size: size), styleMask: floatingStyle(c), backing: .buffered, defer: false)
    }

    /// Makes the main window a panel, as its options ask: an AsukaPanel
    /// takes the content of the window made before them, which nothing has
    /// shown yet.
    func makeMainPanel(_ c: AsukaWindowConfig) {
        let old = main!
        if old.window is NSPanel { return }
        let content = old.content
        let w = newPanelWindow(content.bounds.size, c)
        w.isReleasedWhenClosed = false
        w.title = old.window.title
        NotificationCenter.default.removeObserver(old, name: NSView.frameDidChangeNotification, object: content)
        old.window.contentView = NSView()
        old.window.delegate = nil
        w.contentView = content
        let x = AsukaWin(window: w, content: content, base: old.base)
        x.root = old.root
        x.cfg = old.cfg
        x.configured = old.configured
        x.chrome = old.chrome
        x.shell = old.shell
        // Let go of, not closed: the app is running already, and closing
        // its only window would quit it (applicationShouldTerminateAfter-
        // LastWindowClosed). It never showed.
        old.window.orderOut(nil)
        main = x
    }

    /// Gives a flyout or a panel its look and behavior as its options are
    /// applied: clear under the system's material (or its background),
    /// rounded, shadowed, on every Space for a panel, at the level of menus
    /// for a tray's flyout.
    func floatingChrome(_ x: AsukaWin) {
        let w = x.window, c = x.cfg
        w.isOpaque = false
        w.backgroundColor = .clear
        w.hasShadow = true
        w.hidesOnDeactivate = false
        w.isExcludedFromWindowsMenu = true
        var radius: CGFloat = 12
        var material: NSVisualEffectView.Material = .menu
        var behavior: NSWindow.CollectionBehavior = [.transient, .ignoresCycle, .fullScreenAuxiliary]
        var level: NSWindow.Level?
        if x.isPanel {
            radius = 16
            material = .popover
            behavior.insert(.canJoinAllSpaces)
            level = .floating
        } else if x.shell.flyout.tray != 0 {
            level = .popUpMenu
        }
        w.collectionBehavior = behavior
        if let level { w.level = level }
        let content = x.content
        content.wantsLayer = true
        guard let layer = content.layer else { return }
        layer.cornerRadius = radius
        layer.masksToBounds = true
        if c.flags.contains(.transparent) { return }
        if c.background != 0 {
            layer.backgroundColor = asukaColor(c.background)?.cgColor
            return
        }
        if content.subviews.contains(where: { $0 is NSVisualEffectView && node(of: $0) == nil }) { return }
        // The material, under the content, following its size.
        content.autoresizesSubviews = true
        let ev = NSVisualEffectView(frame: content.bounds)
        ev.material = material
        ev.blendingMode = .behindWindow
        ev.state = .active // as the app isn't
        ev.autoresizingMask = [.width, .height]
        content.addSubview(ev, positioned: .below, relativeTo: nil)
    }

    /// The visible frame (below the menu bar, beside the Dock) of the
    /// display with the point x, y (y down), or of the main display.
    func workArea(_ x: CGFloat, _ y: CGFloat) -> AsukaFlyoutRect {
        for s in NSScreen.screens where asukaYDown(s.frame).contains(x, y) { return asukaYDown(s.visibleFrame) }
        guard let s = NSScreen.screens.first else { return AsukaFlyoutRect(w: 1440, h: 900) }
        return asukaYDown(s.visibleFrame)
    }

    /// Where the mouse is, y down.
    func pointer() -> (CGFloat, CGFloat) {
        let p = NSEvent.mouseLocation
        return (p.x, asukaMainScreenHeight() - p.y)
    }

    /// Whether the point x, y (y down) is on a display.
    func onScreen(_ x: CGFloat, _ y: CGFloat) -> Bool {
        NSScreen.screens.contains { asukaYDown($0.frame).contains(x, y) }
    }

    /// The rectangle x hangs from on the screen, y down: its tray icon's
    /// button, or the rectangle of its parent's content.
    func flyoutAnchor(_ x: AsukaWin) -> AsukaFlyoutRect? {
        let f = x.shell.flyout
        if f.tray != 0 {
            guard let b = AsukaShell.shared.trayButton(id: f.tray), let bw = b.window else { return nil }
            // Clicked: the copy of the button the click hit (one per
            // display's menu bar), while its window keeps its frame.
            if let c = AsukaShell.shared.trayClick(id: f.tray), c.of == bw.frame { return asukaYDown(c.clicked) }
            // Not clicked (opened from code): the item's window, where it's
            // on a display. A Mac whose menu bar has no room for the item,
            // or whose menu bar shows on several displays, may not show it
            // there: then where the pointer is, as a menu would open.
            let r = asukaYDown(bw.frame)
            if onScreen(r.x + r.w / 2, r.y + r.h / 2) { return r }
            let (px, py) = pointer()
            return AsukaFlyoutRect(x: px, y: py)
        }
        let p = parentWindow(x.cfg.parent)
        guard let content = p.contentView else { return nil }
        let r = CGRect(x: CGFloat(f.x), y: CGFloat(f.y), width: CGFloat(f.width), height: CGFloat(f.height))
        return asukaYDown(p.convertToScreen(content.convert(r, to: nil)))
    }

    /// Places x against its anchor, in the work area of the anchor's
    /// display; with no anchor (its tray icon gone), in the middle of the
    /// pointer's display.
    func placeFlyout(_ x: AsukaWin) {
        guard x.isFlyout else { return }
        let size = x.window.frame.size
        let r: AsukaFlyoutRect
        x.shell.placedAgainst = flyoutAnchor(x)
        if let a = x.shell.placedAgainst {
            r = AsukaFlyoutPlacement.place(a, size.width, size.height, x.shell.flyout.placement, CGFloat(x.shell.flyout.gap), workArea(a.x + a.w / 2, a.y + a.h / 2))
        } else {
            let (px, py) = pointer()
            r = AsukaFlyoutPlacement.center(size.width, size.height, workArea(px, py))
        }
        x.window.setFrame(asukaYUp(r), display: true)
    }

    /// Puts a panel the app hasn't placed on the display with the pointer,
    /// centered across it, a third of the way down.
    func centerPanel(_ x: AsukaWin) {
        guard x.isPanel, !x.shell.placed else { return }
        let size = x.window.frame.size
        let (px, py) = pointer()
        x.window.setFrame(asukaYUp(AsukaFlyoutPlacement.center(size.width, size.height, workArea(px, py))), display: true)
    }

    /// Shows a flyout or a panel as the key window without activating the
    /// app: placed, a rectangle's flyout attached to its parent.
    func showFloating(_ x: AsukaWin) {
        let w = x.window
        placeFlyout(x)
        centerPanel(x)
        if ProcessInfo.processInfo.environment["ASUKA_SNAPSHOT"] != nil {
            w.orderFront(nil)
        } else {
            // Key without activating the app: NSApp's isActive reads true
            // while it's key, but the app in front stays so (the
            // workspace's frontmost application).
            w.makeKeyAndOrderFront(nil)
        }
        if x.isFlyout && x.shell.flyout.tray == 0 && w.parent == nil {
            let p = parentWindow(x.cfg.parent)
            if p !== w { p.addChildWindow(w, ordered: .above) }
        }
        watchOutsideClicks()
    }

    /// Hides a flyout or a panel, a flyout no longer its parent's child
    /// (AppKit would show it again with its parent).
    func hideFloating(_ x: AsukaWin) {
        let w = x.window
        w.parent?.removeChildWindow(w)
        w.orderOut(nil)
    }

    /// The flyout or panel a modal Window c's parent is, or nil: the modal
    /// Window is its child window, not a sheet.
    func overFloating(_ c: AsukaWindowConfig) -> AsukaWin? {
        guard c.flags.contains(.modal), let x = win(of: parentWindow(c.parent)), x.cfg.floating else { return nil }
        return x
    }

    /// Centers x, a modal Window, over the flyout or panel it's modal to,
    /// within its display, at its level.
    func placeOver(_ x: AsukaWin, _ over: AsukaWin) {
        x.window.level = over.window.level
        x.window.collectionBehavior = [.transient, .fullScreenAuxiliary]
        let o = asukaYDown(over.window.frame)
        let f = asukaYDown(x.window.frame)
        let work = workArea(o.x + o.w / 2, o.y + o.h / 2)
        var r = AsukaFlyoutRect(x: o.x + (o.w - f.w) / 2, y: o.y + (o.h - f.h) / 2, w: f.w, h: f.h)
        r.x = max(min(r.x, work.right - r.w), work.x)
        r.y = max(min(r.y, work.bottom - r.h), work.y)
        x.window.setFrame(asukaYUp(r), display: false)
    }

    /// Whether a flyout or panel stays open as it loses the keyboard: a
    /// sheet of it, or a modal Window over it, shows.
    func keptOpen(_ x: AsukaWin) -> Bool {
        if x.window.attachedSheet != nil { return true }
        return wins.values.contains { $0.shell.child === x.window && $0.window.isVisible }
    }

    /// Closes a flyout or a panel as the user closes it, if it shows and
    /// isn't kept open; unless outside (a click in another app), only once
    /// another window, or none, is key.
    func dismissFloating(_ x: AsukaWin?, _ outside: Bool) {
        guard let x, x.cfg.floating, x.window.isVisible, !keptOpen(x) else { return }
        if !outside && x.window.isKeyWindow { return }
        userClose(x)
    }

    /// Closes x as its close button would: the core asks the app first if
    /// it listens, a Window closes through the core (Dismissed), the main
    /// window closes (hidden in an app that keeps running).
    func userClose(_ x: AsukaWin) {
        if x.isMain {
            if let r = root, r.listening(.closeRequest) {
                emit(r, .closeRequest) { AsukaEvents.closeRequest($0) }
                return
            }
            hideFloating(x)
            navTarget.hidByApp = false // closed, as its close button would
            x.window.close()
            windowsChanged()
            return
        }
        guard let r = x.root else { return }
        if r.listening(.closeRequest) {
            emit(r, .closeRequest) { AsukaEvents.closeRequest($0) }
        } else {
            emit(r, .dismissed) { AsukaEvents.dismissed($0) }
        }
    }

    /// Closes the flyout or panel w is, on an Escape its content left; it
    /// reports whether w is one.
    func escapeFloating(_ w: NSWindow) -> Bool {
        guard let x = win(of: w), x.cfg.floating else { return false }
        if !keptOpen(x) { userClose(x) }
        return true
    }

    /// Whether the point x, y (y down) is in one of the app's windows that
    /// shows, a flyout's dialog or an alert's too.
    func appWindowAt(_ x: CGFloat, _ y: CGFloat) -> Bool {
        NSApp.windows.contains { $0.isVisible && asukaYDown($0.frame).contains(x, y) }
    }

    /// Watches clicks in other apps once a flyout or a panel shows. Not
    /// under automation: a test's app watches no other apps' clicks.
    func watchOutsideClicks() {
        guard navTarget.outsideClicks == nil, !AsukaAutomation.shared.active else { return }
        navTarget.outsideClicks = NSEvent.addGlobalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown, .otherMouseDown]) { ev in
            // An inactive app's windows get a global monitor's events too: a
            // click on one of its own is no click elsewhere.
            let at = ev.locationInWindow // on the screen: no window of the app's
            MainActor.assumeIsolated {
                let rt = AsukaRuntime.shared
                if rt.appWindowAt(at.x, asukaMainScreenHeight() - at.y) { return }
                for x in rt.allWins { rt.dismissFloating(x, true) }
            }
        }
    }

    /// A window no longer key: a flyout or panel closes once the key window
    /// has settled, unless the key went to its own.
    func resignedKey(_ w: NSWindow) {
        guard let x = win(of: w), x.cfg.floating else { return }
        DispatchQueue.main.async {
            MainActor.assumeIsolated {
                let rt = AsukaRuntime.shared
                guard rt.win(of: x.window) === x else { return }
                if let k = rt.win(of: NSApp.keyWindow), k.shell.child === x.window { return }
                rt.dismissFloating(x, false)
            }
        }
    }

    /// Sizes x's content to width by height points, as its content's size:
    /// a flyout placed again, other windows keeping their top left. A
    /// window waiting for its size shows.
    func fit(_ x: AsukaWin, _ width: Float, _ height: Float) {
        guard width > 0, height > 0 else { return }
        let w = x.window
        let f = w.frame
        let top = CGPoint(x: f.minX, y: f.maxY)
        w.setContentSize(CGSize(width: CGFloat(width), height: CGFloat(height)))
        if x.isFlyout {
            if w.isVisible { placeFlyout(x) }
        } else {
            w.setFrameTopLeftPoint(top)
        }
        if x.isFlyout || x.isPanel { w.invalidateShadow() }
        x.shell.fitted = true
        if x.shell.showOnFit {
            x.shell.showOnFit = false
            showWin(x, AsukaShell.shared.inBackground)
        }
    }

    /// Gives the key back to the flyout or panel a modal Window over it was
    /// the child of, as that closes.
    func regainKey(_ parent: NSWindow) {
        if let x = win(of: parent), x.cfg.floating, parent.isVisible { parent.makeKey() }
    }
}
