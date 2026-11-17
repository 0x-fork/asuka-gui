// Shape: an AsukaShapeView whose layer has a CAShapeLayer filling it, its
// path in the view's (flipped) points.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

/// A Shape's view: it takes no clicks.
final class AsukaShapeView: NSView {
    override var isFlipped: Bool { true }
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        if window != nil, let n = AsukaRuntime.shared.node(of: self) { AsukaRuntime.shared.reassertShadow(n) }
    }
}

/// A Shape node's: its shape layer and colors.
@MainActor final class AsukaShape {
    var layer: CAShapeLayer?
    var fill: UInt64 = 0
    var stroke: UInt64 = 0

    func ensure(_ rt: AsukaRuntime, _ n: AsukaNode) -> CAShapeLayer? {
        if let l = layer { return l }
        guard let parent = rt.layer(n) else { return nil }
        let l = CAShapeLayer()
        l.fillColor = nil
        l.strokeColor = nil
        l.lineWidth = 1
        l.frame = CGRect(origin: .zero, size: n.frame.size)
        parent.addSublayer(l)
        layer = l
        return l
    }

    func setProp(_ rt: AsukaRuntime, _ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {
        guard let l = ensure(rt, n) else { return }
        switch p {
        case AsukaProp.Shape.path:
            set(rt, "path", asukaPath(v.data))
        case AsukaProp.Shape.fill:
            fill = v.color
            rt.withAppearance(n) { colors(n) }
        case AsukaProp.Shape.stroke:
            stroke = v.color
            rt.withAppearance(n) { colors(n) }
        case AsukaProp.Shape.lineWidth:
            l.lineWidth = CGFloat(v.float)
        case AsukaProp.Shape.lineCap:
            l.lineCap = [CAShapeLayerLineCap.butt, .round, .square][Int(min(max(v.int, 0), 2))]
        case AsukaProp.Shape.lineJoin:
            l.lineJoin = [CAShapeLayerLineJoin.miter, .round, .bevel][Int(min(max(v.int, 0), 2))]
        case AsukaProp.Shape.trimStart:
            set(rt, "strokeStart", NSNumber(value: v.float))
        case AsukaProp.Shape.trimEnd:
            set(rt, "strokeEnd", NSNumber(value: v.float))
        case AsukaProp.Shape.dash:
            let b = v.data
            guard b.count >= 8 else {
                l.lineDashPattern = nil
                break
            }
            l.lineDashPhase = CGFloat(b.f32(0))
            var pattern: [NSNumber] = []
            var i = 4
            while i + 4 <= b.count {
                pattern.append(NSNumber(value: b.f32(i)))
                i += 4
            }
            l.lineDashPattern = pattern
        default:
            break
        }
    }

    /// Sets a shape layer's key, animated inside an Animate (a view's
    /// animator doesn't reach a sublayer's).
    func set(_ rt: AsukaRuntime, _ key: String, _ value: Any?) {
        guard let l = layer else { return }
        if rt.animating {
            let a = CABasicAnimation(keyPath: key)
            a.fromValue = (l.presentation() ?? l).value(forKeyPath: key)
            a.toValue = value
            a.duration = rt.animation.duration
            l.add(a, forKey: key)
        }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        l.setValue(value, forKeyPath: key)
        CATransaction.commit()
    }

    /// Sets the colors, in the current appearance.
    func colors(_ n: AsukaNode) {
        guard let l = layer else { return }
        l.fillColor = asukaColor(fill)?.cgColor
        l.strokeColor = asukaColor(stroke)?.cgColor
    }

    func resized(_ n: AsukaNode) {
        guard let l = layer else { return }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        l.frame = CGRect(origin: .zero, size: n.frame.size)
        CATransaction.commit()
    }
}

/// Builds a path from the wire format: u8 PathOp, then its f32 arguments.
/// Cut short, it ends there.
func asukaPath(_ b: [UInt8]) -> CGPath {
    let p = CGMutablePath()
    var i = 0
    var short = false
    func f() -> CGFloat {
        guard i + 4 <= b.count else {
            short = true
            i = b.count
            return 0
        }
        defer { i += 4 }
        return CGFloat(b.f32(i))
    }
    func rect() -> CGRect {
        let x = f(), y = f(), w = f(), h = f()
        return CGRect(x: x, y: y, width: w, height: h)
    }
    while i < b.count && !short {
        let op = AsukaPathOp(rawValue: Int32(b[i]))
        i += 1
        switch op {
        case .move:
            let x = f(), y = f()
            if !short { p.move(to: CGPoint(x: x, y: y)) }
        case .line:
            let x = f(), y = f()
            if !short { p.addLine(to: CGPoint(x: x, y: y)) }
        case .quad:
            let cx = f(), cy = f(), x = f(), y = f()
            if !short { p.addQuadCurve(to: CGPoint(x: x, y: y), control: CGPoint(x: cx, y: cy)) }
        case .cubic:
            let c1x = f(), c1y = f(), c2x = f(), c2y = f(), x = f(), y = f()
            if !short { p.addCurve(to: CGPoint(x: x, y: y), control1: CGPoint(x: c1x, y: c1y), control2: CGPoint(x: c2x, y: c2y)) }
        case .close:
            p.closeSubpath()
        case .rect:
            let r = rect()
            if !short { p.addRect(r) }
        case .roundedRect:
            let r = rect()
            let rad = min(f(), r.width / 2, r.height / 2)
            if !short { p.addRoundedRect(in: r, cornerWidth: max(rad, 0), cornerHeight: max(rad, 0)) }
        case .ellipse:
            let r = rect()
            if !short { p.addEllipse(in: r) }
        case .arc:
            let x = f(), y = f(), rad = f(), start = f(), end = f(), cw = f()
            // The layer is flipped (y down), as UIKit's: the screen's
            // clockwise is Core Graphics' flag false.
            if !short { p.addArc(center: CGPoint(x: x, y: y), radius: rad, startAngle: start, endAngle: end, clockwise: cw == 0) }
        default:
            return p
        }
    }
    return p
}

@MainActor extension AsukaRuntime {
    func makeShape(_ n: AsukaNode) -> NSView {
        n.shape = AsukaShape()
        return AsukaShapeView(frame: .zero)
    }

    func setShapeProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {
        n.shape?.setProp(self, n, p, v)
    }

    func shapeJSON(_ s: AsukaShape, _ d: inout [String: Any]) {
        guard let l = s.layer else { return }
        var j: [String: Any] = ["lineWidth": Double(l.lineWidth), "lineCap": l.lineCap.rawValue, "lineJoin": l.lineJoin.rawValue,
                                "strokeStart": Double(l.strokeStart), "strokeEnd": Double(l.strokeEnd),
                                "frame": [Double(l.frame.minX), Double(l.frame.minY), Double(l.frame.width), Double(l.frame.height)]]
        if let p = l.path {
            let b = p.boundingBoxOfPath
            j["pathBox"] = [Double(b.minX), Double(b.minY), Double(b.width), Double(b.height)]
        }
        if let c = l.fillColor { j["fill"] = cgColorHex(c) }
        if let c = l.strokeColor { j["stroke"] = cgColorHex(c) }
        if let dash = l.lineDashPattern { j["dash"] = dash.map { $0.doubleValue }; j["dashPhase"] = Double(l.lineDashPhase) }
        if let a = l.animation(forKey: "strokeEnd") { j["strokeEndAnimation"] = a.duration }
        d["shape"] = j
    }
}
