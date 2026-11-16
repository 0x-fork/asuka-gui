// std/shader on iOS and macOS: a CAMetalLayer the program's Metal
// library draws, a triangle over the whole box (asuka_vertex,
// asuka_fragment), its uniforms in a buffer as the code lays them out.
// The view sets rectSize, pixelScale and Time; pipelines are made once
// per library. Animated, it draws at each frame of a display link; else
// when its size, program or uniforms change.
import Asuka
import Foundation
import Metal
import QuartzCore
#if canImport(UIKit)
import UIKit
#else
import AppKit
#endif

/// A program's code (internal/shadercode's Encode): its Metal library,
/// where each lane goes in its buffer, and its uniforms' lanes.
struct ShaderCode {
    struct Var { var lane = 0, lanes = 0, kind = 0 }  // kind: 0 float, 1 int, 2 uint, 3 bool
    var metal = ""
    var lanes = 0, size = 0, scale = 0, time = -1
    var metalSize = 0
    var offsets: [Int] = [], bools: [Bool] = []
    var vars: [String: Var] = [:]

    init?(_ d: Data) {
        var at = d.startIndex
        var bad = false
        func u32() -> Int {
            guard at + 4 <= d.endIndex else { bad = true; at = d.endIndex; return 0 }
            defer { at += 4 }
            return Int(UInt32(d[at]) | UInt32(d[at + 1]) << 8 | UInt32(d[at + 2]) << 16 | UInt32(d[at + 3]) << 24)
        }
        func str() -> String {
            let n = u32()
            guard !bad, at + n <= d.endIndex else { bad = true; return "" }
            defer { at += n }
            return String(decoding: d[at..<at + n], as: UTF8.self)
        }
        _ = str()  // the Kage source: the desktop's
        metal = str()
        _ = str()  // AGSL: Android's
        lanes = u32(); size = u32(); scale = u32(); time = u32() - 1
        metalSize = u32()
        guard !bad, lanes < 1 << 16 else { return nil }
        for _ in 0..<lanes {
            let o = u32()
            offsets.append(o & 0x7fff_ffff)
            bools.append(o & 0x8000_0000 != 0)
        }
        let n = u32()
        for _ in 0..<n where !bad {
            let name = str()
            var v = Var(lane: u32(), lanes: u32())
            if at < d.endIndex { v.kind = Int(d[at]); at += 1 }
            vars[name] = v
        }
        if bad { return nil }
    }

    /// The lanes the uniforms set: a float's bits, an int's or a bool's.
    func words(_ us: [Uniform]) -> [UInt32] {
        var w = [UInt32](repeating: 0, count: lanes)
        for u in us {
            guard let v = vars[u.name], !u.name.hasPrefix("__") else { continue }
            for (i, x) in u.value.prefix(v.lanes).enumerated() where v.lane + i < lanes {
                switch v.kind {
                case 0: w[v.lane + i] = Float(x).bitPattern
                case 1: w[v.lane + i] = UInt32(bitPattern: Int32(clamping: Int(x.rounded())))
                case 2: w[v.lane + i] = UInt32(clamping: Int(max(0, x.rounded())))
                default: w[v.lane + i] = x != 0 ? 1 : 0
                }
            }
        }
        return w
    }
}

@MainActor final class ShaderRenderer {
    static let device = MTLCreateSystemDefaultDevice()
    static let queue = device?.makeCommandQueue()
    static var pipelines: [String: MTLRenderPipelineState] = [:]
    static var failed: Set<String> = []
    /// What the library's image parameters take: a Shader reads no images.
    static let blank: MTLTexture? = {
        let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: 1, height: 1, mipmapped: false)
        return device?.makeTexture(descriptor: d)
    }()

    let layer = CAMetalLayer()
    var code: ShaderCode? { didSet { pipeline = nil; words = code?.words(uniforms) ?? [] } }
    var uniforms: [Uniform] = [] { didSet { words = code?.words(uniforms) ?? [] } }
    private var words: [UInt32] = []
    private var pipeline: MTLRenderPipelineState?
    private var buffer: [UInt8] = []
    var start = CACurrentMediaTime()

    init() {
        layer.device = Self.device
        layer.pixelFormat = .bgra8Unorm
        layer.isOpaque = false
        layer.framebufferOnly = true
    }

    private func makePipeline(_ source: String) -> MTLRenderPipelineState? {
        if let p = Self.pipelines[source] { return p }
        guard let device = Self.device, !Self.failed.contains(source) else { return nil }
        do {
            let lib = try device.makeLibrary(source: source, options: nil)
            let d = MTLRenderPipelineDescriptor()
            d.vertexFunction = lib.makeFunction(name: "asuka_vertex")
            d.fragmentFunction = lib.makeFunction(name: "asuka_fragment")
            d.colorAttachments[0].pixelFormat = .bgra8Unorm
            let p = try device.makeRenderPipelineState(descriptor: d)
            Self.pipelines[source] = p
            return p
        } catch {
            NSLog("asuka: a shader's Metal library doesn't compile: %@", "\(error)")
            Self.failed.insert(source)
            return nil
        }
    }

    /// Draws a frame in a box of size points, scale pixels a point.
    func draw(size: CGSize, scale: CGFloat) {
        guard let code, size.width > 0, size.height > 0 else { return }
        layer.contentsScale = scale
        let px = CGSize(width: (size.width * scale).rounded(), height: (size.height * scale).rounded())
        if layer.drawableSize != px { layer.drawableSize = px }
        if pipeline == nil { pipeline = makePipeline(code.metal) }
        guard let pipeline, let queue = Self.queue, let drawable = layer.nextDrawable(),
              let cmd = queue.makeCommandBuffer() else { return }
        if words.count < code.lanes { words += [UInt32](repeating: 0, count: code.lanes - words.count) }
        words[code.size] = Float(size.width).bitPattern
        words[code.size + 1] = Float(size.height).bitPattern
        words[code.scale] = Float(scale).bitPattern
        if code.time >= 0 { words[code.time] = Float(CACurrentMediaTime() - start).bitPattern }
        if buffer.count != max(code.metalSize, 16) { buffer = [UInt8](repeating: 0, count: max(code.metalSize, 16)) }
        for i in 0..<code.lanes {
            let o = code.offsets[i]
            if code.bools[i] {
                buffer[o] = words[i] != 0 ? 1 : 0
            } else {
                buffer.withUnsafeMutableBytes { $0.storeBytes(of: words[i].littleEndian, toByteOffset: o, as: UInt32.self) }
            }
        }
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = drawable.texture
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 0)
        pass.colorAttachments[0].storeAction = .store
        guard let enc = cmd.makeRenderCommandEncoder(descriptor: pass) else { return }
        enc.setRenderPipelineState(pipeline)
        buffer.withUnsafeBytes { enc.setFragmentBytes($0.baseAddress!, length: $0.count, index: 0) }
        for i in 0..<4 { enc.setFragmentTexture(Self.blank, index: i) }
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
    }
}

#if canImport(UIKit)
final class ShaderCanvas: UIView {
    let renderer = ShaderRenderer()
    var animated = false { didSet { updateLink() } }
    private var link: CADisplayLink?

    override init(frame: CGRect) {
        super.init(frame: frame)
        layer.addSublayer(renderer.layer)
        isUserInteractionEnabled = false
        isAccessibilityElement = false
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) isn't used") }

    override func sizeThatFits(_ size: CGSize) -> CGSize { .zero }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        renderer.start = CACurrentMediaTime()
        updateLink()
        draw()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        renderer.layer.frame = bounds
        CATransaction.commit()
        draw()
    }

    func draw() {
        guard window != nil else { return }
        renderer.draw(size: bounds.size, scale: window?.screen.scale ?? traitCollection.displayScale)
    }

    private func updateLink() {
        let on = animated && window != nil
        if on, link == nil {
            let l = CADisplayLink(target: self, selector: #selector(tick))
            l.add(to: .main, forMode: .common)
            link = l
        } else if !on, let l = link {
            l.invalidate()
            link = nil
        }
    }

    @objc private func tick() { draw() }
}
#else
final class ShaderCanvas: NSView {
    let renderer = ShaderRenderer()
    var animated = false { didSet { updateTimer() } }
    private var timer: Timer?

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layer?.addSublayer(renderer.layer)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) isn't used") }

    override var isFlipped: Bool { true }
    override var fittingSize: NSSize { .zero }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        renderer.start = CACurrentMediaTime()
        updateTimer()
        draw()
    }

    override func layout() {
        super.layout()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        renderer.layer.frame = bounds
        CATransaction.commit()
        draw()
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        draw()
    }

    func draw() {
        guard let w = window else { return }
        renderer.draw(size: bounds.size, scale: w.backingScaleFactor)
    }

    /// Animated: a frame each 60th of a second while it's in a window.
    private func updateTimer() {
        let on = animated && window != nil
        if on, timer == nil {
            let t = Timer(timeInterval: 1.0 / 60, repeats: true) { [weak self] _ in
                MainActor.assumeIsolated { self?.draw() }
            }
            RunLoop.main.add(t, forMode: .common)
            timer = t
        } else if !on, let t = timer {
            t.invalidate()
            timer = nil
        }
    }
}
#endif

@MainActor final class MetalShader: Shader {
    let canvas = ShaderCanvas(frame: .zero)
    var view: AsukaPlatformView { canvas }
    init(_ events: ShaderEvents) {}
    func setProgram(_ value: Program) {
        canvas.renderer.code = ShaderCode(value.code)
        canvas.draw()
    }
    func setUniforms(_ value: [Uniform]) {
        canvas.renderer.uniforms = value
        canvas.draw()
    }
    func setAnimated(_ value: Bool) { canvas.animated = value }
}

@MainActor func makeShader(_ events: ShaderEvents) -> any Shader { MetalShader(events) }
