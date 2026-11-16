// asuka/svg on iOS and macOS: the icon's layers (the compiler's encoding:
// sdk/svg's Encode) as CGPaths, drawn by Core Graphics in a view of the
// package's, fitted to its bounds; currentColor is the view's color,
// resolved as the platform's color (a semantic one follows light and
// dark).
import Asuka
import CoreGraphics
import Foundation
#if canImport(UIKit)
import UIKit
#else
import AppKit
#endif

/// An icon's layers, read from its data.
struct Drawing {
    enum Paint { case none, rgba(CGFloat, CGFloat, CGFloat, CGFloat), current(CGFloat) }
    struct Layer {
        var fill = Paint.none, stroke = Paint.none
        var lineWidth: CGFloat = 1
        var cap = CGLineCap.butt, join = CGLineJoin.miter
        var path = CGMutablePath()
    }
    var viewBox = CGRect.zero
    var size = CGSize.zero
    var layers: [Layer] = []

    init(_ d: Data) {
        var at = d.startIndex
        func u8() -> UInt8 { guard at < d.endIndex else { return 0 }; defer { at += 1 }; return d[at] }
        func u32() -> UInt32 {
            guard at + 4 <= d.endIndex else { at = d.endIndex; return 0 }
            defer { at += 4 }
            return UInt32(d[at]) | UInt32(d[at + 1]) << 8 | UInt32(d[at + 2]) << 16 | UInt32(d[at + 3]) << 24
        }
        func f32() -> CGFloat { CGFloat(Float(bitPattern: u32())) }
        guard d.count >= 24 else { return }
        viewBox = CGRect(x: f32(), y: f32(), width: f32(), height: f32())
        size = CGSize(width: f32(), height: f32())
        func paint() -> Paint {
            switch u8() {
            case 1:
                let r = CGFloat(u8()) / 255, g = CGFloat(u8()) / 255, b = CGFloat(u8()) / 255, a = CGFloat(u8()) / 255
                return .rgba(r, g, b, a)
            case 2: return .current(f32())
            default: return .none
            }
        }
        let n = Int(u32())
        for _ in 0..<n where at < d.endIndex {
            var l = Layer()
            l.fill = paint()
            l.stroke = paint()
            l.lineWidth = f32()
            l.cap = [.butt, .round, .square][min(Int(u8()), 2)]
            l.join = [.miter, .round, .bevel][min(Int(u8()), 2)]
            let end = at + Int(u32())
            let p = CGMutablePath()
            while at < min(end, d.endIndex) {
                switch u8() {
                case 1: p.move(to: CGPoint(x: f32(), y: f32()))
                case 2: p.addLine(to: CGPoint(x: f32(), y: f32()))
                case 3:
                    let c = CGPoint(x: f32(), y: f32())
                    p.addQuadCurve(to: CGPoint(x: f32(), y: f32()), control: c)
                case 4:
                    let c1 = CGPoint(x: f32(), y: f32()), c2 = CGPoint(x: f32(), y: f32())
                    p.addCurve(to: CGPoint(x: f32(), y: f32()), control1: c1, control2: c2)
                case 5: p.closeSubpath()
                case 6: p.addRect(CGRect(x: f32(), y: f32(), width: f32(), height: f32()))
                case 7:
                    let r = CGRect(x: f32(), y: f32(), width: f32(), height: f32()), rad = f32()
                    p.addRoundedRect(in: r, cornerWidth: min(rad, r.width / 2), cornerHeight: min(rad, r.height / 2))
                case 8: p.addEllipse(in: CGRect(x: f32(), y: f32(), width: f32(), height: f32()))
                default: at = end
                }
            }
            at = end
            l.path = p
            layers.append(l)
        }
    }

    /// Draws the layers fitted to bounds (centered, the aspect kept),
    /// currentColor color.
    func draw(_ c: CGContext, in bounds: CGRect, color: CGColor) {
        guard viewBox.width > 0, viewBox.height > 0 else { return }
        let scale = min(bounds.width / viewBox.width, bounds.height / viewBox.height)
        let dx = bounds.minX + (bounds.width - viewBox.width * scale) / 2 - viewBox.minX * scale
        let dy = bounds.minY + (bounds.height - viewBox.height * scale) / 2 - viewBox.minY * scale
        func cg(_ p: Paint) -> CGColor? {
            switch p {
            case .none: return nil
            case .rgba(let r, let g, let b, let a): return CGColor(srgbRed: r, green: g, blue: b, alpha: a)
            case .current(let o): return o >= 1 ? color : color.copy(alpha: color.alpha * o)
            }
        }
        c.saveGState()
        c.translateBy(x: dx, y: dy)
        c.scaleBy(x: scale, y: scale)
        for l in layers {
            if let f = cg(l.fill) {
                c.addPath(l.path)
                c.setFillColor(f)
                c.fillPath()
            }
            if let s = cg(l.stroke), l.lineWidth > 0 {
                c.addPath(l.path)
                c.setStrokeColor(s)
                c.setLineWidth(l.lineWidth)
                c.setLineCap(l.cap)
                c.setLineJoin(l.join)
                c.strokePath()
            }
        }
        c.restoreGState()
    }
}

#if canImport(UIKit)
final class IconView: UIView {
    var drawing = Drawing(Data()) { didSet { setNeedsDisplay() } }
    var color: Int = 0 { didSet { setNeedsDisplay() } }

    override init(frame: CGRect) {
        super.init(frame: frame)
        isOpaque = false
        backgroundColor = .clear
        contentMode = .redraw
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) isn't used") }

    override func sizeThatFits(_ size: CGSize) -> CGSize { drawing.size }

    override func draw(_ rect: CGRect) {
        guard let c = UIGraphicsGetCurrentContext() else { return }
        let col = (AsukaColors.color(color) ?? .label).resolvedColor(with: traitCollection).cgColor
        drawing.draw(c, in: bounds, color: col)
    }

    override func traitCollectionDidChange(_ previous: UITraitCollection?) {
        super.traitCollectionDidChange(previous)
        setNeedsDisplay()
    }
}
#else
final class IconView: NSView {
    var drawing = Drawing(Data()) { didSet { needsDisplay = true } }
    var color: Int = 0 { didSet { needsDisplay = true } }

    override var isFlipped: Bool { true }
    override var fittingSize: NSSize { drawing.size }

    override func draw(_ dirty: NSRect) {
        guard let c = NSGraphicsContext.current?.cgContext else { return }
        var col = CGColor(gray: 0, alpha: 1)
        effectiveAppearance.performAsCurrentDrawingAppearance {
            col = (AsukaColors.color(color) ?? .labelColor).cgColor
        }
        drawing.draw(c, in: bounds, color: col)
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        needsDisplay = true
    }
}
#endif

@MainActor final class SvgView: View {
    let icon = IconView(frame: .zero)
    var view: AsukaPlatformView { icon }
    init(_ events: ViewEvents) {}
    func setIcon(_ value: Icon) { icon.drawing = Drawing(value.data) }
    func setColor(_ value: Int) { icon.color = value }
}

@MainActor func makeView(_ events: ViewEvents) -> any View { SvgView(events) }
