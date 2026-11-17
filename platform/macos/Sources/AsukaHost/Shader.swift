// Shader: a view whose layer holds a CAMetalLayer, under its subviews,
// drawn by the shader's Metal library (the MSL the compiler writes, package
// shadercode's Code), a triangle over the whole box, its uniforms in a
// buffer as the code lays them out; the host sets the size, the scale and
// Time. Animated shaders draw at each tick of a clock of their own (a
// display link); the others when their size or uniforms change.
import AppKit
import Metal
import QuartzCore
#if canImport(AsukaOps)
import AsukaOps
#endif

/// A compiled ui.Shader, as the core sends it (shadercode.Code's wire):
/// its Metal library, and where each uniform lane goes in its buffer.
struct AsukaShaderCode {
    var metal = ""
    var lanes = 0, size = 0, scale = 0, time = -1
    var metalSize = 0
    var offsets: [Int] = [], bools: [Bool] = []

    init?(_ b: [UInt8]) {
        var r = AsukaByteReader(b)
        _ = r.string() // the Kage source: the CPU's
        metal = r.string()
        _ = r.string() // AGSL: Android's
        lanes = Int(r.u32()); size = Int(r.u32()); scale = Int(r.u32()); time = Int(r.u32()) - 1
        metalSize = Int(r.u32())
        guard !r.bad, lanes <= 1 << 16 else { return nil } // as shadercode.Decode
        for _ in 0..<lanes where !r.bad {
            let o = r.u32()
            offsets.append(Int(o & 0x7fff_ffff))
            bools.append(o & 0x8000_0000 != 0)
        }
        guard !r.bad, size + 1 < lanes, scale < lanes, time < lanes else { return nil }
        // Every lane in the buffer.
        let buf = max(metalSize, 16)
        for (i, o) in offsets.enumerated() where o + (bools[i] ? 1 : 4) > buf { return nil }
    }
}

/// A Shader node's: its layer, code and uniforms.
@MainActor final class AsukaShader {
    var layer: CAMetalLayer?
    var code: AsukaShaderCode?
    /// The uniforms' lanes.
    var words: [UInt32] = []
    /// The Metal buffer.
    var buf: [UInt8] = []
    var animated = false
    var pipeline: MTLRenderPipelineState?
    var failed = false
    var start = CACurrentMediaTime()
    /// Frames drawn (the automation's: it draws, and an animated one goes on).
    var drawn = 0

    static let device: MTLDevice? = MTLCreateSystemDefaultDevice()
    static let queue: MTLCommandQueue? = device?.makeCommandQueue()
    /// Pipelines, made once per library.
    static var pipelines: [String: MTLRenderPipelineState] = [:]
    /// What the library's image parameters take: ui.Shader reads no images.
    static let blank: MTLTexture? = {
        let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: 1, height: 1, mipmapped: false)
        return device?.makeTexture(descriptor: d)
    }()

    func makePipeline(_ src: String) -> MTLRenderPipelineState? {
        if let p = Self.pipelines[src] { return p }
        guard let device = Self.device else { return nil }
        do {
            let lib = try device.makeLibrary(source: src, options: nil)
            let d = MTLRenderPipelineDescriptor()
            d.vertexFunction = lib.makeFunction(name: "asuka_vertex")
            d.fragmentFunction = lib.makeFunction(name: "asuka_fragment")
            d.colorAttachments[0].pixelFormat = .bgra8Unorm
            let p = try device.makeRenderPipelineState(descriptor: d)
            Self.pipelines[src] = p
            return p
        } catch {
            FileHandle.standardError.write(Data("asuka: a ui.Shader's Metal library doesn't compile: \(error.localizedDescription)\n".utf8))
            return nil
        }
    }
}

/// The animated shaders' clock: a display link where the system has view
/// display links (14), else a timer at 60 Hz.
@MainActor final class AsukaFrameClockTarget: NSObject {
    private var link: AnyObject?
    private var timer: Timer?

    init(view: NSView) {
        super.init()
        if #available(macOS 14.0, *) {
            let l = view.displayLink(target: self, selector: #selector(tick(_:)))
            l.add(to: .main, forMode: .common)
            link = l
        } else {
            let t = Timer(timeInterval: 1.0 / 60, repeats: true) { _ in
                MainActor.assumeIsolated { AsukaRuntime.shared.shaderTick() }
            }
            t.tolerance = 0
            RunLoop.main.add(t, forMode: .common)
            timer = t
        }
    }

    @objc func tick(_ sender: Any?) { AsukaRuntime.shared.shaderTick() }

    func stop() {
        if #available(macOS 14.0, *) { (link as? CADisplayLink)?.invalidate() }
        timer?.invalidate()
    }
}

@MainActor extension AsukaRuntime {
    func makeShader(_ n: AsukaNode) -> NSView {
        let s = AsukaShader()
        n.shader = s
        let v = AsukaView(frame: .zero)
        v.wantsLayer = true
        if let device = AsukaShader.device {
            let l = CAMetalLayer()
            l.device = device
            l.pixelFormat = .bgra8Unorm
            l.isOpaque = false
            l.framebufferOnly = true
            v.layer?.insertSublayer(l, at: 0)
            s.layer = l
        }
        return v
    }

    func setShaderProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {
        guard let s = n.shader else { return }
        switch p {
        case AsukaProp.Shader.code:
            guard let c = AsukaShaderCode(v.data) else { return }
            s.code = c
            s.pipeline = nil
            s.failed = false
            if s.words.count != c.lanes { s.words = Array(repeating: 0, count: c.lanes) }
        case AsukaProp.Shader.animated:
            s.animated = v.bool
            updateShaderClock()
        case AsukaProp.Shader.uniforms:
            let b = v.data
            s.words = stride(from: 0, to: b.count - 3, by: 4).map { b.u32($0) }
        default:
            break
        }
        drawShader(n)
    }

    /// Sizes the Metal layer to its view's bounds, in pixels.
    func layoutShader(_ n: AsukaNode) {
        guard let s = n.shader, let l = s.layer, let v = n.view else { return }
        let b = v.bounds
        let k = v.window?.backingScaleFactor ?? 2
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        l.frame = b
        l.contentsScale = k
        l.drawableSize = CGSize(width: b.width * k, height: b.height * k)
        CATransaction.commit()
        drawShader(n)
    }

    func drawShader(_ n: AsukaNode) {
        guard let s = n.shader, let l = s.layer, let c = s.code, let queue = AsukaShader.queue, let v = n.view else { return }
        let b = v.bounds
        guard b.width > 0, b.height > 0, v.window != nil else { return }
        if s.pipeline == nil && !s.failed {
            s.pipeline = s.makePipeline(c.metal)
            s.failed = s.pipeline == nil
        }
        guard let pipeline = s.pipeline else { return }
        if s.words.count < c.lanes { s.words += Array(repeating: 0, count: c.lanes - s.words.count) }
        s.words[c.size] = Float(b.width).bitPattern
        s.words[c.size + 1] = Float(b.height).bitPattern
        s.words[c.scale] = Float(l.contentsScale).bitPattern
        if c.time >= 0 { s.words[c.time] = Float(CACurrentMediaTime() - s.start).bitPattern }
        let size = max(c.metalSize, 16)
        if s.buf.count != size { s.buf = Array(repeating: 0, count: size) }
        for i in 0..<c.lanes {
            let o = c.offsets[i]
            if c.bools[i] {
                s.buf[o] = UInt8(min(s.words[i], 1))
            } else {
                let w = s.words[i]
                s.buf.withUnsafeMutableBytes { $0.storeBytes(of: w.littleEndian, toByteOffset: o, as: UInt32.self) }
            }
        }
        guard let drawable = l.nextDrawable(), let cmd = queue.makeCommandBuffer() else { return }
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = drawable.texture
        pass.colorAttachments[0].loadAction = .dontCare // the triangle covers every pixel
        pass.colorAttachments[0].storeAction = .store
        guard let enc = cmd.makeRenderCommandEncoder(descriptor: pass) else { return }
        enc.setRenderPipelineState(pipeline)
        s.buf.withUnsafeBytes { enc.setFragmentBytes($0.baseAddress!, length: $0.count, index: 0) }
        for i in 0..<4 { enc.setFragmentTexture(AsukaShader.blank, index: i) }
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
        s.drawn += 1
    }

    /// Runs the animated shaders' clock while any is shown.
    func updateShaderClock() {
        let on = nodes.values.contains { $0.shader?.animated ?? false }
        if on == (visual.shaderClock != nil) { return }
        if !on {
            visual.shaderClock?.stop()
            visual.shaderClock = nil
            return
        }
        visual.shaderClock = AsukaFrameClockTarget(view: main.content)
    }

    func shaderTick() {
        for n in nodes.values where n.shader?.animated ?? false { drawShader(n) }
    }

    func shaderJSON(_ n: AsukaNode, _ s: AsukaShader, _ d: inout [String: Any]) {
        var j: [String: Any] = ["animated": s.animated, "drawn": s.drawn, "failed": s.failed, "pipeline": s.pipeline != nil,
                                "code": s.code != nil, "clock": visual.shaderClock != nil]
        if let l = s.layer {
            j["drawableSize"] = [Double(l.drawableSize.width), Double(l.drawableSize.height)]
            j["layerFrame"] = [Double(l.frame.minX), Double(l.frame.minY), Double(l.frame.width), Double(l.frame.height)]
            j["layerIndex"] = n.view?.layer?.sublayers?.firstIndex(of: l) ?? -1
        }
        d["shader"] = j
    }
}
