// ProgressRing: an AsukaRingView. At AppKit's own sizes, 16 points (small)
// and 32 (regular), in the system's colors, it holds AppKit's
// NSProgressIndicator in its spinning style: its circle, determinate, and
// its spinner without a value. AppKit draws those at its control sizes only
// (a larger frame centers the regular one; scaled, its circle comes apart
// and its spinner, a bitmap, blurs) and in the accent color only, so at any
// other size, or in the Style's Color, the ring draws what AppKit draws, as
// large as its frame's shorter side, with Core Animation layers: the
// circle's track and arc, and the spinner's spokes. Their colors are read
// from what AppKit draws (an unseen indicator of its own), so they follow
// the accent, the appearance and whether the window is active as AppKit's
// do.
//
// The AsukaRingView is the accessibility element either way, read as
// AppKit's indicator is: a progress indicator with its value, from 0 to 1
// (VoiceOver reads its percentage), or, without a value, a busy indicator.
// Ports host/darwin's progressring.go.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

/// The sizes AppKit draws its spinning NSProgressIndicator at: its
/// regular control size's (the default) and its small one's.
let asukaRingRegular: CGFloat = 32
let asukaRingSmall: CGFloat = 16

/// AppKit's spinner, measured: 8 spokes with round ends, from 3/16 of its
/// size out to its edge, an eighth of its size wide (4 points at 32, 2 at
/// 16). It turns clockwise in 24 steps a turn, 0.8 seconds: the leading
/// spoke darkest, each behind it an eighth lighter.
let asukaSpinnerSpokes = 8
let asukaSpinnerSteps = 24
let asukaSpinnerTurn = 0.8

/// How long AppKit's animator takes to move an NSProgressIndicator's value
/// (NSAnimationContext's default; linear).
let asukaRingValueDuration = 0.25

/// The width of the circle's stroke at a size. AppKit's circle is a
/// 5-point stroke at both its sizes, its outer edge the frame's: at 32
/// points about a sixth of the ring, at 16 nearly a third. The ring keeps
/// the regular circle's proportions above 32 points and the small one's
/// below 16, and the 5 points between.
func asukaRingStroke(_ side: CGFloat) -> CGFloat {
    if side >= asukaRingRegular { return side * 5 / asukaRingRegular }
    if side <= asukaRingSmall { return side * 5 / asukaRingSmall }
    return 5
}

/// AppKit's colors, as read from its indicator: the arc, and the arc of a
/// full circle (in dark mode a lighter blue), the track and the spinner's
/// leading spoke, sRGB with alpha.
struct AsukaRingLook {
    var arc: [CGFloat], full: [CGFloat], track: [CGFloat], spoke: [CGFloat]
    /// Read; else read again.
    var ok = false

    // What AppKit draws, measured (sRGB, the system's blue accent): the
    // colors used until they're read from its own indicator (a view not in
    // a window yet).
    static let arcLight: [CGFloat] = [10.0 / 255, 130.0 / 255, 1, 1]
    static let arcDark: [CGFloat] = [23.0 / 255, 105.0 / 255, 230.0 / 255, 1]
    static let fullDark: [CGFloat] = [23.0 / 255, 123.0 / 255, 229.0 / 255, 1]
    static let inactiveLight: [CGFloat] = [185.0 / 255, 185.0 / 255, 185.0 / 255, 1]
    static let inactiveDark: [CGFloat] = [1, 1, 1, 83.0 / 255]
    static let trackLight: [CGFloat] = [0, 0, 0, 13.0 / 255]
    static let trackDark: [CGFloat] = [1, 1, 1, 26.0 / 255]
    static let spokeLight: [CGFloat] = [0, 0, 0, 140.0 / 255]
    static let spokeDark: [CGFloat] = [1, 1, 1, 140.0 / 255]

    static let light = AsukaRingLook(arc: arcLight, full: arcLight, track: trackLight, spoke: spokeLight)
    static let dark = AsukaRingLook(arc: arcDark, full: fullDark, track: trackDark, spoke: spokeDark)

    /// The arc's color at a value: AppKit's, or a full circle's.
    func arcAt(_ v: Double) -> [CGFloat] { v >= 1 ? full : arc }
}

func asukaSRGB(_ c: [CGFloat]) -> NSColor { NSColor(srgbRed: c[0], green: c[1], blue: c[2], alpha: c[3]) }

/// A ProgressRing's view: what VoiceOver reads, as AppKit's indicator
/// answers (an NSView's own leave its role unknown and its value out).
final class AsukaRingView: NSView {
    override var isFlipped: Bool { true }

    @MainActor var ring: AsukaRing? { AsukaRuntime.shared.node(of: self)?.ring }

    override func accessibilityRole() -> NSAccessibility.Role? {
        guard let r = ring else { return super.accessibilityRole() }
        return r.indeterminate ? .busyIndicator : .progressIndicator
    }
    override func accessibilityValue() -> Any? {
        guard let r = ring else { return super.accessibilityValue() }
        return r.indeterminate ? NSNumber(value: 0) : NSNumber(value: r.value)
    }
    override func accessibilityMinValue() -> Any? {
        guard let r = ring else { return super.accessibilityMinValue() }
        return r.indeterminate ? nil : NSNumber(value: 0)
    }
    override func accessibilityMaxValue() -> Any? {
        guard let r = ring else { return super.accessibilityMaxValue() }
        return r.indeterminate ? nil : NSNumber(value: 1)
    }
    override func isAccessibilityElement() -> Bool { true }
    override func accessibilityChildren() -> [Any]? { [] }

    // Answered through the attributes too, as AppKit's indicator answers:
    // an NSView's own leave its role unknown and its value out, whatever
    // the methods above say.
    override func accessibilityAttributeValue(_ attr: NSAccessibility.Attribute) -> Any? {
        if let r = ring, let v = r.attribute(attr.rawValue) { return v }
        return super.accessibilityAttributeValue(attr)
    }
    override func accessibilityAttributeNames() -> [NSAccessibility.Attribute] {
        var names = super.accessibilityAttributeNames()
        guard let r = ring else { return names }
        for a in ["AXValue", "AXMinValue", "AXMaxValue"] where r.attribute(a) != nil {
            let k = NSAccessibility.Attribute(rawValue: a)
            if !names.contains(k) { names.append(k) }
        }
        return names
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        let rt = AsukaRuntime.shared
        if let n = rt.node(of: self), let r = n.ring { r.restyle(rt, n) }
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        let rt = AsukaRuntime.shared
        guard window != nil, let n = rt.node(of: self) else { return }
        rt.reassertShadow(n)
        n.ring?.restyle(rt, n) // the window's state
    }
}

/// Runs f without Core Animation's implicit animations.
func asukaNoActions(_ f: () -> Void) {
    CATransaction.begin()
    CATransaction.setDisableActions(true)
    f()
    CATransaction.commit()
}

/// An NSProgressIndicator in the spinning style, from 0 to 1, read by
/// VoiceOver through the AsukaRingView.
@MainActor func asukaIndicator(_ indeterminate: Bool, _ value: Double) -> NSProgressIndicator {
    let v = NSProgressIndicator(frame: .zero)
    v.style = .spinning
    v.minValue = 0
    v.maxValue = 1
    v.isIndeterminate = indeterminate
    v.doubleValue = value
    v.setAccessibilityElement(false) // the AsukaRingView is the element
    return v
}

/// A ProgressRing node's.
@MainActor final class AsukaRing {
    var value: Double = 0
    var indeterminate = false
    /// AppKit's NSProgressIndicator, when it draws the ring.
    var native: NSProgressIndicator?
    /// The drawn circle.
    var track: CAShapeLayer?, arc: CAShapeLayer?
    /// The drawn spinner: a replicator of spokes, and its spoke.
    var spinner: CAReplicatorLayer?, spoke: CAShapeLayer?
    /// AppKit's indicator, unseen, drawn to read its colors.
    var ref: NSProgressIndicator?
    var look = AsukaRingLook.light
    /// The last value change's animation, in seconds (0: at once): the
    /// automation's.
    var lastAnimation: Double = 0
    /// Every ring, while it lives (the automation's: what they hold).
    static var all = NSHashTable<AnyObject>.weakObjects()

    /// Whether AppKit's own indicator draws the ring at side: one of its
    /// sizes, in its colors.
    func nativeAt(_ n: AsukaNode, _ side: CGFloat) -> Bool {
        n.color == 0 && (side == asukaRingRegular || side == asukaRingSmall)
    }

    func set(_ rt: AsukaRuntime, _ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {
        switch p {
        case AsukaProp.ProgressRing.value:
            setValue(rt, n, v.float)
        case AsukaProp.ProgressRing.indeterminate:
            guard indeterminate != v.bool else { return }
            indeterminate = v.bool
            look.ok = false // the spinner's colors are read from AppKit's spinner
            if let nv = native {
                nv.isIndeterminate = indeterminate
                nv.doubleValue = value
                if indeterminate { nv.startAnimation(nil) } else { nv.stopAnimation(nil) }
            }
            if let view = n.view { accessibility(view) }
            layout(rt, n)
        default:
            break
        }
    }

    /// Fits the ring to its view's size: AppKit's indicator, centered, at
    /// its sizes and in its colors; else the drawn ring, as large as the
    /// shorter side. It runs as the frame or the Color changes.
    func layout(_ rt: AsukaRuntime, _ n: AsukaNode) {
        guard n.placed, let view = n.view else { return } // its first frame lays it out
        let size = n.frame.size
        let side = max(min(size.width, size.height), 0)
        let square = CGRect(x: (size.width - side) / 2, y: (size.height - side) / 2, width: side, height: side)
        if nativeAt(n, side) {
            removeDrawn()
            let nv = ensureNative(view)
            let cs: NSControl.ControlSize = side == asukaRingSmall ? .small : .regular
            if nv.controlSize != cs { nv.controlSize = cs }
            nv.frame = square
            return
        }
        removeNative()
        asukaNoActions {
            if indeterminate {
                removeCircle()
                layoutSpinner(view, square)
            } else {
                removeSpinner()
                layoutCircle(n, view, square)
            }
        }
        colors(rt, n)
    }

    func ensureNative(_ view: NSView) -> NSProgressIndicator {
        if let nv = native { return nv }
        let nv = asukaIndicator(indeterminate, value)
        nv.isDisplayedWhenStopped = false
        view.addSubview(nv)
        native = nv
        if indeterminate { nv.startAnimation(nil) }
        return nv
    }

    func removeNative() {
        guard let nv = native else { return }
        nv.stopAnimation(nil)
        nv.removeFromSuperview()
        native = nil
    }

    func removeDrawn() {
        asukaNoActions {
            removeCircle()
            removeSpinner()
        }
        if let r = ref {
            r.removeFromSuperview()
            ref = nil
            look.ok = false
        }
    }

    /// Lets go of what the ring holds, as its view goes.
    func release() {
        removeNative()
        removeDrawn()
    }

    func removeCircle() {
        track?.removeFromSuperlayer()
        arc?.removeFromSuperlayer()
        track = nil
        arc = nil
    }

    func removeSpinner() {
        spinner?.removeFromSuperlayer()
        spinner = nil
        spoke = nil
    }

    /// Draws the circle in square: from the top, clockwise, its stroke's
    /// outer edge the square's, the arc over the track as far as the value,
    /// its ends round.
    func layoutCircle(_ n: AsukaNode, _ view: NSView, _ square: CGRect) {
        if track == nil {
            view.wantsLayer = true
            let t = CAShapeLayer(), a = CAShapeLayer()
            for l in [t, a] {
                l.fillColor = nil
                l.lineCap = .round
                view.layer?.addSublayer(l)
            }
            a.strokeEnd = CGFloat(value)
            track = t
            arc = a
        }
        let side = square.width
        let w = asukaRingStroke(side)
        // The layer is flipped (y down): the screen's clockwise is Core
        // Graphics' flag false.
        let p = CGMutablePath()
        p.addArc(center: CGPoint(x: square.minX + side / 2, y: square.minY + side / 2), radius: max(side / 2 - w / 2, 0),
                 startAngle: -.pi / 2, endAngle: 3 * .pi / 2, clockwise: false)
        let b = CGRect(origin: .zero, size: n.frame.size)
        for l in [track, arc].compactMap({ $0 }) {
            l.frame = b
            l.path = p
            l.lineWidth = w
            l.isHidden = side <= 0
        }
    }

    /// Draws AppKit's spinner in square: a spoke, from the top, replicated
    /// round the center, each copy a step of the turn behind the one
    /// before, its opacity falling an eighth a spoke, in steps.
    func layoutSpinner(_ view: NSView, _ square: CGRect) {
        let side = square.width
        if spinner == nil {
            view.wantsLayer = true
            let r = CAReplicatorLayer(), s = CAShapeLayer()
            s.fillColor = nil
            s.lineCap = .round
            r.addSublayer(s)
            r.instanceCount = asukaSpinnerSpokes
            r.instanceDelay = asukaSpinnerTurn / Double(asukaSpinnerSpokes)
            // A clockwise turn on screen is a positive angle, y down.
            r.instanceTransform = CATransform3DMakeRotation(2 * .pi / CGFloat(asukaSpinnerSpokes), 0, 0, 1)
            view.layer?.addSublayer(r)
            spinner = r
            spoke = s
            spin()
        }
        guard let r = spinner, let s = spoke else { return }
        r.frame = square
        s.frame = CGRect(origin: .zero, size: square.size)
        let w = side / 8
        // From 3/16 of the size out to the edge, the round ends included.
        let p = CGMutablePath()
        p.move(to: CGPoint(x: side / 2, y: side / 2 - (side * 3 / 16 + w / 2)))
        p.addLine(to: CGPoint(x: side / 2, y: w / 2))
        s.path = p
        s.lineWidth = w
        r.isHidden = side <= 0
    }

    /// Starts the spokes' animation, a turn already under way so every
    /// spoke shows its place in it from the start. AppKit's spinner turns
    /// whether or not Reduce Motion is on, and so does this.
    func spin() {
        let a = CAKeyframeAnimation(keyPath: "opacity")
        a.values = (0..<asukaSpinnerSteps).map { NSNumber(value: 1 - Double($0) / Double(asukaSpinnerSteps)) }
        a.calculationMode = .discrete
        a.duration = asukaSpinnerTurn
        a.repeatCount = .infinity
        a.beginTime = CACurrentMediaTime() - asukaSpinnerTurn
        a.isRemovedOnCompletion = false
        spoke?.add(a, forKey: "asuka.spin")
    }

    /// Moves the ring to v, as AppKit's animator moves its indicator's
    /// value: over a quarter second, linearly, or with the Animate it's in;
    /// at once when Reduce Motion is on, or before it shows.
    func setValue(_ rt: AsukaRuntime, _ n: AsukaNode, _ v: Double) {
        let from = value
        value = v
        if !indeterminate { n.view?.setAccessibilityValue(NSNumber(value: v)) }
        let animate = n.placed && from != v && n.view?.window != nil && (rt.animating || !rt.env.reduceMotion)
        if arc != nil && n.color == 0 && (from >= 1) != (v >= 1) {
            colors(rt, n) // a full circle's blue
        }
        lastAnimation = 0
        if let nv = native {
            if !animate || indeterminate {
                nv.doubleValue = v
                return
            }
            if rt.animating { // the Animate's context
                lastAnimation = rt.animation.duration
                nv.animator().doubleValue = v
                return
            }
            lastAnimation = asukaRingValueDuration
            NSAnimationContext.beginGrouping()
            NSAnimationContext.current.duration = asukaRingValueDuration
            nv.animator().doubleValue = v
            NSAnimationContext.endGrouping()
            return
        }
        guard let a = arc else { return }
        let key = "strokeEnd"
        if animate {
            let now = a.presentation() ?? a
            let anim = CABasicAnimation(keyPath: key)
            anim.fromValue = NSNumber(value: Double(now.strokeEnd))
            anim.toValue = NSNumber(value: v)
            if rt.animating {
                anim.duration = rt.animation.duration
                anim.timingFunction = NSAnimationContext.current.timingFunction
            } else {
                anim.duration = asukaRingValueDuration
            }
            lastAnimation = anim.duration
            a.add(anim, forKey: key)
        } else {
            a.removeAnimation(forKey: key)
        }
        asukaNoActions { a.strokeEnd = CGFloat(v) }
    }

    /// Makes the view read as AppKit's indicator: a progress indicator with
    /// its value, minimum and maximum, or a busy indicator.
    func accessibility(_ v: NSView) {
        v.setAccessibilityElement(true)
        if indeterminate {
            v.setAccessibilityRole(.busyIndicator)
            v.setAccessibilityValue(NSNumber(value: 0))
            v.setAccessibilityMinValue(nil)
            v.setAccessibilityMaxValue(nil)
            return
        }
        v.setAccessibilityRole(.progressIndicator)
        v.setAccessibilityValue(NSNumber(value: value))
        v.setAccessibilityMinValue(NSNumber(value: 0))
        v.setAccessibilityMaxValue(NSNumber(value: 1))
    }

    /// What the ring answers for an accessibility attribute, as AppKit's
    /// indicator: its role, its value, from 0 to 1; a busy indicator's
    /// value 0, with no range. nil: the view's own.
    func attribute(_ name: String) -> Any? {
        switch name {
        case "AXRole": return indeterminate ? "AXBusyIndicator" : "AXProgressIndicator"
        case "AXValue": return indeterminate ? NSNumber(value: 0) : NSNumber(value: value)
        case "AXMinValue": return indeterminate ? nil : NSNumber(value: 0)
        case "AXMaxValue": return indeterminate ? nil : NSNumber(value: 1)
        default: return nil
        }
    }

    /// Reads AppKit's colors again (the appearance, the window's state or
    /// the accent changed) and sets them.
    func restyle(_ rt: AsukaRuntime, _ n: AsukaNode) {
        look.ok = false
        colors(rt, n)
    }

    /// Sets the drawn ring's colors: the track AppKit's; the arc and the
    /// spokes the Style's Color, or AppKit's.
    func colors(_ rt: AsukaRuntime, _ n: AsukaNode) {
        guard track != nil || spoke != nil, let view = n.view else { return }
        if !look.ok { read(view) }
        let apply = {
            let custom = asukaColor(n.color)
            asukaNoActions {
                if let t = self.track {
                    t.strokeColor = asukaSRGB(self.look.track).cgColor
                    self.arc?.strokeColor = (custom ?? asukaSRGB(self.look.arcAt(self.value))).cgColor
                }
                // The leading spoke the Color.
                self.spoke?.strokeColor = (custom ?? asukaSRGB(self.look.spoke)).cgColor
            }
        }
        if #available(macOS 11.0, *) { view.effectiveAppearance.performAsCurrentDrawingAppearance(apply) } else { apply() }
    }

    /// Reads AppKit's colors from an indicator of its own in the ring's
    /// view, unseen: drawn into a bitmap, its pixels as they are (AppKit's
    /// drawing is sRGB, and a device RGB bitmap leaves it so). The circle,
    /// half full, gives the arc and the track; the spinner, stopped, the
    /// darkest of its spokes. Out of a window, or should the drawing hold
    /// nothing, it's what AppKit draws as measured.
    func read(_ view: NSView) {
        let dark = view.effectiveAppearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua
        var l = dark ? AsukaRingLook.dark : AsukaRingLook.light
        if !asukaWindowActive(view.window) {
            l.arc = dark ? AsukaRingLook.inactiveDark : AsukaRingLook.inactiveLight
            l.full = l.arc
        }
        look = l
        guard view.window != nil else { return }
        let r: NSProgressIndicator
        if let have = ref {
            r = have
        } else {
            r = asukaIndicator(false, 0.5)
            r.frame = CGRect(x: 0, y: 0, width: asukaRingRegular, height: asukaRingRegular)
            r.isDisplayedWhenStopped = true
            r.alphaValue = 0
            view.addSubview(r)
            ref = r
        }
        // Transparent, AppKit draws it otherwise (another blue): it's opaque
        // while it's drawn here, and transparent again before the window
        // shows anything.
        asukaNoActions { r.alphaValue = 1 }
        defer { asukaNoActions { r.alphaValue = 0 } }
        let scale = 2
        let b = CGRect(x: 0, y: 0, width: asukaRingRegular, height: asukaRingRegular)
        func draw() -> NSBitmapImageRep? {
            guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(asukaRingRegular) * scale, pixelsHigh: Int(asukaRingRegular) * scale,
                                             bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                             colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return nil }
            rep.size = b.size
            r.cacheDisplay(in: b, to: rep)
            return rep
        }
        func at(_ rep: NSBitmapImageRep?, _ radius: CGFloat, _ deg: CGFloat) -> [CGFloat] {
            let a = deg * .pi / 180
            let x = (asukaRingRegular / 2 + radius * sin(a)) * CGFloat(scale)
            let y = (asukaRingRegular / 2 - radius * cos(a)) * CGFloat(scale)
            guard let c = rep?.colorAt(x: Int(x), y: Int(y)) else { return [0, 0, 0, 0] }
            return [c.redComponent, c.greenComponent, c.blueComponent, c.alphaComponent]
        }
        if indeterminate {
            r.isIndeterminate = true
            let rep = draw()
            var best: [CGFloat] = [0, 0, 0, 0]
            for i in 0..<asukaSpinnerSpokes {
                let c = at(rep, 11, CGFloat(i) * 360 / CGFloat(asukaSpinnerSpokes))
                if c[3] > best[3] { best = c }
            }
            if best[3] > 0 { l.spoke = best }
            r.isIndeterminate = false
        } else {
            // The stroke's middle, 13.5 points out: the arc at 45 degrees,
            // the track at 270; and a full circle's arc.
            r.doubleValue = 0.5
            let rep = draw()
            let c = at(rep, 13.5, 45)
            if c[3] > 0 { l.arc = c }
            let t = at(rep, 13.5, 270)
            if t[3] > 0 { l.track = t }
            r.doubleValue = 1
            l.full = l.arc
            let f = at(draw(), 13.5, 270)
            if f[3] > 0 { l.full = f }
        }
        l.ok = true
        look = l
    }
}

/// Whether w draws its controls active, in the accent: the key or main
/// window of the active app.
@MainActor func asukaWindowActive(_ w: NSWindow?) -> Bool {
    guard let w, NSApp.isActive else { return false }
    return w.isKeyWindow || w.isMainWindow
}

@MainActor extension AsukaRuntime {
    func makeRing(_ n: AsukaNode) -> NSView {
        if !visual.ringsObserved {
            visual.ringsObserved = true
            // The drawn rings' colors follow their windows' active state and
            // the system's accent. AppKit redraws its own as the window's
            // state changes, after the notification: they're read on the
            // next turn of the run loop.
            let names: [Notification.Name] = [
                NSWindow.didBecomeKeyNotification, NSWindow.didResignKeyNotification,
                NSWindow.didBecomeMainNotification, NSWindow.didResignMainNotification,
                NSApplication.didBecomeActiveNotification, NSApplication.didResignActiveNotification,
                NSColor.systemColorsDidChangeNotification,
            ]
            for name in names {
                NotificationCenter.default.addObserver(visual, selector: #selector(AsukaVisual.ringsChanged(_:)), name: name, object: nil)
            }
        }
        let r = AsukaRing()
        AsukaRing.all.add(r)
        n.ring = r
        let v = AsukaRingView(frame: .zero)
        v.wantsLayer = true
        r.accessibility(v)
        return v
    }

    func setRingProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {
        n.ring?.set(self, n, p, v)
    }

    /// AppKit's regular control size's (32 points), or square: a style that
    /// sizes one side sizes the other.
    func measureRing(_ n: AsukaNode, _ c: AsukaOffer) -> CGSize {
        if c.widthKnown { return CGSize(width: CGFloat(c.width), height: CGFloat(c.width)) }
        if c.heightKnown { return CGSize(width: CGFloat(c.height), height: CGFloat(c.height)) }
        return CGSize(width: asukaRingRegular, height: asukaRingRegular)
    }

    /// The Color changed: AppKit's indicator draws in the accent color
    /// only, so the ring is drawn in another.
    func ringColor(_ n: AsukaNode) {
        if n.placed, let r = n.ring { r.layout(self, n) }
    }

    /// Reads AppKit's colors again for every drawn ring.
    func restyleRings() {
        for n in nodes.values { if let r = n.ring { r.restyle(self, n) } }
    }

    func ringJSON(_ n: AsukaNode, _ r: AsukaRing, _ d: inout [String: Any]) {
        func rect(_ f: CGRect) -> [Double] { [Double(f.minX), Double(f.minY), Double(f.width), Double(f.height)] }
        func rgba(_ c: CGColor?) -> [Double] {
            guard let c, let s = NSColor(cgColor: c)?.usingColorSpace(.sRGB) else { return [] }
            return [s.redComponent, s.greenComponent, s.blueComponent, s.alphaComponent].map { Double($0) }
        }
        var j: [String: Any] = ["value": r.value, "indeterminate": r.indeterminate, "native": r.native != nil,
                                "track": r.track != nil, "arc": r.arc != nil, "spinner": r.spinner != nil, "spoke": r.spoke != nil,
                                "ref": r.ref != nil, "lookRead": r.look.ok, "lastAnimation": r.lastAnimation,
                                "windowActive": asukaWindowActive(n.view?.window), "reduceMotion": env.reduceMotion]
        if n.color != 0, let v = n.view {
            var c: [Double] = []
            let get = { c = rgba(asukaColor(n.color)?.cgColor) }
            if #available(macOS 11.0, *) { v.effectiveAppearance.performAsCurrentDrawingAppearance(get) } else { get() }
            j["colorExpected"] = c
        }
        if let nv = r.native {
            j["nativeStyle"] = nv.style.rawValue
            j["nativeControlSize"] = nv.controlSize.rawValue
            j["nativeFrame"] = rect(nv.frame)
            j["nativeValue"] = nv.doubleValue
            j["nativeIndeterminate"] = nv.isIndeterminate
            j["nativeA11yElement"] = nv.isAccessibilityElement()
        }
        if let s = r.spinner, let sp = r.spoke {
            j["spokes"] = s.instanceCount
            j["spinnerFrame"] = rect(s.frame)
            j["spokeWidth"] = Double(sp.lineWidth)
            if let p = sp.path { j["spokeBox"] = rect(p.boundingBoxOfPath) }
            j["spinning"] = sp.animation(forKey: "asuka.spin") != nil
            j["spokeColor"] = rgba(sp.strokeColor)
        }
        if let a = r.arc, let t = r.track {
            j["strokeWidth"] = Double(a.lineWidth)
            j["trackWidth"] = Double(t.lineWidth)
            j["lineCap"] = a.lineCap.rawValue
            j["trackCap"] = t.lineCap.rawValue
            if let p = a.path { j["arcBox"] = rect(p.boundingBoxOfPath) }
            if let p = t.path { j["trackBox"] = rect(p.boundingBoxOfPath) }
            j["strokeEnd"] = Double(a.strokeEnd)
            j["arcColor"] = rgba(a.strokeColor)
            j["trackColor"] = rgba(t.strokeColor)
            if let an = a.animation(forKey: "strokeEnd") { j["strokeEndAnimation"] = an.duration }
        }
        // What VoiceOver reads of it, and of AppKit's own indicator in its
        // state.
        if let v = n.view {
            j["a11y"] = asukaA11yAttributes(v)
            let ref = asukaIndicator(r.indeterminate, r.value)
            j["appKitA11y"] = asukaA11yAttributes(ref)
            j["a11yChildren"] = (v.accessibilityChildren() ?? []).count
        }
        d["ring"] = j
    }
}

@MainActor extension AsukaRing {
    /// Whether it holds any of AppKit's indicators or drawn layers.
    var holds: Bool { native != nil || ref != nil || track != nil || arc != nil || spinner != nil || spoke != nil }
}

/// An element's role, role description, value, minimum and maximum, as
/// the accessibility API answers them (nil: "nil").
@MainActor func asukaA11yAttributes(_ v: NSView) -> [String: String] {
    var out: [String: String] = [:]
    for attr in ["AXRole", "AXRoleDescription", "AXValue", "AXMinValue", "AXMaxValue"] {
        let x = v.accessibilityAttributeValue(NSAccessibility.Attribute(rawValue: attr))
        out[attr] = x.map { "\($0)" } ?? "nil"
    }
    return out
}

extension AsukaVisual {
    @objc func ringsChanged(_ note: Notification) {
        NSObject.cancelPreviousPerformRequests(withTarget: self, selector: #selector(restyleRings), object: nil)
        perform(#selector(restyleRings), with: nil, afterDelay: 0)
    }

    @objc func restyleRings() { AsukaRuntime.shared.restyleRings() }
}
