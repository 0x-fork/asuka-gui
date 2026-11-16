// A Snackbar: AppKit has none, so it's an overlay at the bottom of the
// window it's on, in the system's material (an NSVisualEffectView,
// rounded, with a shadow): its message, its action's button and a close
// button. It fades and rises in, and out (a fade with Reduce Motion); its
// time stops while the mouse is over it; VoiceOver announces it
// (NSAccessibility's announcement) and reaches its buttons.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

@MainActor final class AsukaSnack {
    /// The shadow, placed in the window's content.
    var box: AsukaSnackbarView?
    /// The material, rounded.
    var effect: NSVisualEffectView?
    /// The message, wrapping.
    var label: NSTextField?
    /// Its action (nil: none).
    var button: NSButton?
    /// Closes it.
    var close: NSButton?
    /// The content view it's in.
    weak var on: NSView?

    var duration = AsukaSnackbarDuration.short
    var timer: Timer?
    /// Of its time, while it's paused.
    var left: TimeInterval = 0
    var started = Date()
    var hovered = false
    /// Reported (its action, a timeout, closing) or destroyed.
    var done = false
    var leaving = false
}

private let snackMargin: CGFloat = 20 // from the window's edges
private let snackMaxW: CGFloat = 480 // at most this wide, as a toast reads
private let snackPadX: CGFloat = 14
private let snackPadY: CGFloat = 8
private let snackGap: CGFloat = 8
private let snackRadius: CGFloat = 10
private let snackMinH: CGFloat = 36
private let snackCloseSz: CGFloat = 20

/// A snackbar's view: flipped, its tracking area pausing its time under the
/// mouse; VoiceOver's cancel (escape) closes it.
final class AsukaSnackbarView: NSView {
    override var isFlipped: Bool { true }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if trackingAreas.isEmpty {
            addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseEnteredAndExited, .activeInActiveApp, .inVisibleRect], owner: self, userInfo: nil))
        }
    }

    override func mouseEntered(with event: NSEvent) { MainActor.assumeIsolated { AsukaRuntime.shared.snackHover(self, true) } }
    override func mouseExited(with event: NSEvent) { MainActor.assumeIsolated { AsukaRuntime.shared.snackHover(self, false) } }

    override func accessibilityPerformCancel() -> Bool {
        MainActor.assumeIsolated {
            let rt = AsukaRuntime.shared
            guard let n = rt.snackOf(self) else { return false }
            rt.snackReport(n, .dismissed)
            return true
        }
    }
}

@MainActor extension AsukaRuntime {
    /// The snackbar node whose view (or button) v is.
    func snackOf(_ v: NSView) -> AsukaNode? {
        guard let n = node(of: v), n.kind == .snackbar, n.snack != nil else { return nil }
        return n
    }

    func setSnackProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {
        guard let s = n.snack else { return }
        switch p {
        case AsukaProp.Snackbar.message:
            n.message = v.string
            if let l = s.label {
                l.stringValue = v.string
                s.box?.setAccessibilityLabel(v.string)
                placeSnack(n)
            }
        case AsukaProp.Snackbar.action:
            n.title = v.string
            if s.box != nil {
                snackButton(n)
                placeSnack(n)
            }
        case AsukaProp.Snackbar.duration:
            s.duration = AsukaSnackbarDuration(rawValue: Int32(truncatingIfNeeded: v.int)) ?? .short
        case AsukaProp.Snackbar.window:
            n.nav?.on = UInt32(truncatingIfNeeded: v.int)
        default:
            break
        }
    }

    /// Shows n at the bottom of its window's content.
    func presentSnack(_ n: AsukaNode) {
        guard let s = n.snack, s.box == nil, !s.done, let w = onWin(n) else { return }
        s.on = w.content
        let box = AsukaSnackbarView(frame: .zero)
        box.wantsLayer = true
        if let l = box.layer {
            l.shadowOpacity = 0.12
            l.shadowRadius = 8
            l.shadowOffset = CGSize(width: 0, height: -3)
        }
        box.setAccessibilityElement(true)
        box.setAccessibilityRole(.group)
        box.setAccessibilityLabel(n.message)
        s.box = box
        byView[ObjectIdentifier(box)] = n

        let effect = NSVisualEffectView(frame: .zero)
        effect.material = .popover // a light or dark panel over the content
        effect.blendingMode = .withinWindow // it blurs the content
        effect.state = .active // as the window isn't always
        effect.wantsLayer = true
        if let el = effect.layer {
            el.cornerRadius = snackRadius
            el.cornerCurve = .continuous
            el.masksToBounds = true
            el.borderWidth = 0.5
        }
        box.addSubview(effect)
        s.effect = effect

        let label = NSTextField(wrappingLabelWithString: n.message)
        label.isSelectable = false
        label.font = .systemFont(ofSize: 0) // the system size
        label.textColor = .labelColor
        effect.addSubview(label)
        s.label = label

        let closeWord = asukaWord("close")
        let img = NSImage(systemSymbolName: "xmark", accessibilityDescription: closeWord) ?? NSImage()
        let close = NSButton(image: img, target: self, action: #selector(act(_:)))
        close.isBordered = false
        close.imageScaling = .scaleProportionallyDown
        close.contentTintColor = .secondaryLabelColor
        close.toolTip = closeWord
        close.setAccessibilityLabel(closeWord)
        effect.addSubview(close)
        s.close = close
        byView[ObjectIdentifier(close)] = n
        snackButton(n)
        snackBorder(n)

        w.content.addSubview(box)
        placeSnack(n)

        // In: rising a little as it fades in; a fade with Reduce Motion.
        let f = box.frame
        box.alphaValue = 0
        if !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion { box.frame = f.offsetBy(dx: 0, dy: 16) }
        asukaAnimate(0.25, {
            box.animator().alphaValue = 1
            box.animator().frame = f
        }, {})

        var text = n.message
        if !n.title.isEmpty { text += ", " + n.title }
        announce(text)
        startSnack(n)
    }

    /// Makes, retitles or removes its action's button.
    func snackButton(_ n: AsukaNode) {
        guard let s = n.snack else { return }
        if n.title.isEmpty {
            if let b = s.button {
                byView.removeValue(forKey: ObjectIdentifier(b))
                b.removeFromSuperview()
                s.button = nil
            }
            return
        }
        if let b = s.button {
            b.title = n.title
            return
        }
        let b = NSButton(title: n.title, target: self, action: #selector(act(_:)))
        b.bezelStyle = .rounded // push
        s.effect?.addSubview(b)
        s.button = b
        byView[ObjectIdentifier(b)] = n
    }

    /// Sets its hairline in the separator's color, for the appearance it's
    /// in (a layer's CGColor doesn't follow it).
    func snackBorder(_ n: AsukaNode) {
        guard let s = n.snack, let e = s.effect else { return }
        let a = s.on?.window?.effectiveAppearance ?? NSApp.effectiveAppearance
        let prev = NSAppearance.current
        NSAppearance.current = a
        e.layer?.borderColor = NSColor.separatorColor.cgColor
        NSAppearance.current = prev
    }

    /// Lays it out: as wide as its message needs, up to snackMaxW and the
    /// window's width, centered at the bottom; the message, then its button
    /// and the close button (mirrored right to left).
    func placeSnack(_ n: AsukaNode) {
        guard let s = n.snack, let box = s.box, let on = s.on, let label = s.label, let effect = s.effect, let close = s.close,
              !s.leaving else { return }
        let cb = on.bounds
        let maxW = min(snackMaxW, cb.width - 2 * snackMargin)
        var bw: CGFloat = 0, bh: CGFloat = 0
        if let b = s.button {
            let bs = b.fittingSize
            bw = ceil(bs.width)
            bh = ceil(bs.height)
        }
        var fixed = 2 * snackPadX + snackGap + snackCloseSz
        if bw > 0 { fixed += bw + snackGap }
        var lw = max(maxW - fixed, 60)
        label.preferredMaxLayoutWidth = lw
        // The cell's size wrapped at lw: a label's fittingSize wraps at its
        // frame's width, which it hasn't yet.
        let ls = label.cell?.cellSize(forBounds: CGRect(x: 0, y: 0, width: lw, height: 1e6)) ?? .zero
        lw = min(ceil(ls.width), lw)
        let lh = ceil(ls.height)
        let height = max(snackMinH, max(lh, max(bh, snackCloseSz)) + 2 * snackPadY)
        let width = lw + fixed
        let x = ((cb.width - width) / 2).rounded()
        let y = on.isFlipped ? cb.height - snackMargin - height : snackMargin
        let f = CGRect(x: x, y: y, width: width, height: height)
        box.frame = f
        effect.frame = CGRect(origin: .zero, size: f.size)
        // The shadow follows the rounded card, not what's drawn in it.
        box.layer?.shadowPath = CGPath(roundedRect: CGRect(origin: .zero, size: f.size), cornerWidth: snackRadius, cornerHeight: snackRadius, transform: nil)
        // Within the effect view (not flipped), each centered on its height.
        func place(_ v: NSView, _ x: CGFloat, _ w: CGFloat, _ hh: CGFloat) {
            let x = rtl ? width - x - w : x
            v.frame = CGRect(x: x, y: ((height - hh) / 2).rounded(), width: w, height: hh)
        }
        place(label, snackPadX, lw, lh)
        var at = snackPadX + lw + snackGap
        if let b = s.button {
            place(b, at, bw, bh)
            at += bw + snackGap
        }
        place(close, at, snackCloseSz, snackCloseSz)
        on.addSubview(box, positioned: .above, relativeTo: nil) // over what the core adds
    }

    /// Lays out the snackbars showing: a window resized.
    func placeSnacks() {
        for n in nodes.values where n.kind == .snackbar && n.snack?.box != nil {
            placeSnack(n)
            snackBorder(n)
        }
    }

    /// A press of its action's button or its close button.
    func snackPressed(_ n: AsukaNode, _ sender: Any?) {
        if let c = n.snack?.close, (sender as? NSButton) === c {
            snackReport(n, .dismissed)
            return
        }
        snackReport(n, .action)
    }

    /// Tells the core it went (its action, its time running out, closed),
    /// and takes it out; the core destroys it.
    func snackReport(_ n: AsukaNode, _ kind: AsukaEventKind) {
        guard let s = n.snack, !s.done else { return }
        s.done = true
        stopSnackTimer(n)
        leaveSnack(n)
        if kind == .action {
            emit(n, .action) { AsukaEvents.action($0, 0) } // its one action
        } else {
            emit(n, .dismissed) { AsukaEvents.dismissed($0) }
        }
    }

    /// Starts its time: four seconds, ten for SnackbarLong. With VoiceOver
    /// on, one with a button stays until it's closed, and one without stays
    /// long, as Material does without a recommended timeout.
    func startSnack(_ n: AsukaNode) {
        guard let s = n.snack, s.duration != .indefinite else { return }
        var d: TimeInterval = s.duration == .long ? 10 : 4
        if NSWorkspace.shared.isVoiceOverEnabled {
            if !n.title.isEmpty { return }
            d = max(d, 10)
        }
        s.left = d
        if !s.hovered { resumeSnack(n) }
    }

    func resumeSnack(_ n: AsukaNode) {
        guard let s = n.snack, s.timer == nil, s.left > 0, !s.done else { return }
        s.started = Date()
        let id = n.id
        let t = Timer(timeInterval: s.left, repeats: false) { _ in
            // After the timer's call.
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    let rt = AsukaRuntime.shared
                    if let n = rt.nodes[id], n.snack != nil { rt.snackReport(n, .dismissed) }
                }
            }
        }
        RunLoop.main.add(t, forMode: .common)
        s.timer = t
    }

    func stopSnackTimer(_ n: AsukaNode) {
        guard let s = n.snack, let t = s.timer else { return }
        t.invalidate()
        s.timer = nil
        s.left = max(1, s.left - Date().timeIntervalSince(s.started))
    }

    /// Pauses its time while the mouse is over it.
    func snackHover(_ box: NSView, _ on: Bool) {
        guard let n = snackOf(box), let s = n.snack, s.hovered != on else { return }
        s.hovered = on
        if on { stopSnackTimer(n) } else { resumeSnack(n) }
    }

    /// Fades it out (sinking a little), then lets it go.
    func leaveSnack(_ n: AsukaNode) {
        guard let s = n.snack, !s.leaving else { return }
        s.leaving = true
        guard let box = s.box else { return }
        for v in [box, s.button, s.close].compactMap({ $0 as NSView? }) where node(of: v) === n {
            byView.removeValue(forKey: ObjectIdentifier(v))
        }
        box.setAccessibilityElement(false)
        var f = box.frame
        if !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion { f.origin.y += 12 }
        asukaAnimate(0.2, {
            box.animator().alphaValue = 0
            box.animator().frame = f
        }, { box.removeFromSuperview() })
        s.box = nil
    }

    /// The core's Destroy: out, animated.
    func destroySnack(_ n: AsukaNode) {
        n.snack?.done = true
        stopSnackTimer(n)
        leaveSnack(n)
    }

    /// Presses a snackbar's action (by its title) or its close button (by
    /// its label), as a click would.
    func tapSnack(_ label: String) -> Bool {
        for n in nodes.values where n.kind == .snackbar {
            guard let s = n.snack, s.box != nil, !s.done else { continue }
            if let b = s.button, b.title == label {
                snackPressed(n, b)
                return true
            }
            if let c = s.close, c.toolTip == label || c.accessibilityLabel() == label {
                snackPressed(n, c)
                return true
            }
        }
        return false
    }

    /// Moves the mouse over a snackbar, or off it (the automation's
    /// "snackhover").
    func hoverSnack(_ on: Bool) -> Bool {
        for n in nodes.values where n.kind == .snackbar {
            if let b = n.snack?.box {
                snackHover(b, on)
                return true
            }
        }
        return false
    }

    func snackJSON(_ n: AsukaNode, _ d: inout [String: Any]) {
        guard let s = n.snack else { return }
        d["message"] = n.message
        d["action"] = n.title
        d["snackShown"] = s.box != nil
        d["snackDone"] = s.done
        d["paused"] = s.hovered
        d["timing"] = s.timer != nil
        d["duration"] = Int(s.duration.rawValue)
        if let box = s.box {
            d["snackFrame"] = asukaRectJSON(box.frame)
            if let on = s.on { d["onBounds"] = asukaRectJSON(on.bounds); d["onWindow"] = on.window?.title ?? "" }
            d["snackLabel"] = box.accessibilityLabel() ?? ""
            d["snackMaterial"] = s.effect.map { Int($0.material.rawValue) } ?? -1
        }
        d["snackButton"] = s.button?.title ?? ""
        d["snackHasClose"] = s.close != nil
        if let c = s.close { d["closeLabel"] = c.accessibilityLabel() ?? "" }
    }
}

/// Runs f in an animation group of d seconds, then done.
@MainActor func asukaAnimate(_ d: Double, _ f: @escaping () -> Void, _ done: @escaping () -> Void) {
    NSAnimationContext.runAnimationGroup({ ctx in
        ctx.duration = d
        ctx.allowsImplicitAnimation = true
        f()
    }, completionHandler: { MainActor.assumeIsolated { done() } })
}
