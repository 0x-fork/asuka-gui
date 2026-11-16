// STUB (part: visual). Images (loading, placeholders, transitions,
// animated frames), symbols, shapes, canvases, shaders (Metal, the MSL the
// compiler writes), progress rings, video, web views (and the web and js
// modules), native views (ui.Native and packages' typed views). Ports
// host/darwin's imageextra.go, media.go's images, shape.go, canvas.go,
// shader.go, progressring.go, video.go, web.go, webmore.go, native.go.
// The visual agent replaces this file (and may split it), keeping these
// functions' signatures.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

@MainActor final class AsukaShape {}
@MainActor final class AsukaRing {}
@MainActor final class AsukaShader {}
@MainActor final class AsukaWeb {}
@MainActor final class AsukaVideo {}
@MainActor final class AsukaImageState {}

@MainActor extension AsukaRuntime {
    func makeImage(_ n: AsukaNode, _ kind: AsukaKind) -> NSView { AsukaImageView(frame: .zero) }
    func setImageProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func measureImage(_ n: AsukaNode, _ c: AsukaOffer) -> CGSize { (n.view as? NSImageView)?.image?.size ?? .zero }
    /// An SF Symbol's image (nil: none of that name).
    func symbolImage(_ name: String) -> NSImage? {
        guard !name.isEmpty, #available(macOS 11.0, *) else { return nil }
        return NSImage(systemSymbolName: name, accessibilityDescription: nil)
    }
    func applySymbol(_ n: AsukaNode) {}
    func makeRing(_ n: AsukaNode) -> NSView { AsukaView(frame: .zero) }
    func setRingProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func measureRing(_ n: AsukaNode, _ c: AsukaOffer) -> CGSize { CGSize(width: 32, height: 32) }
    func ringColor(_ n: AsukaNode) {}
    func makeCanvas(_ n: AsukaNode) -> NSView { AsukaView(frame: .zero) }
    func makeShader(_ n: AsukaNode) -> NSView { AsukaView(frame: .zero) }
    func setShaderProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func makeShape(_ n: AsukaNode) -> NSView { AsukaView(frame: .zero) }
    func setShapeProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func makeWeb(_ n: AsukaNode) -> NSView { AsukaView(frame: .zero) }
    func setWebProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func webCommand(_ n: AsukaNode, _ c: AsukaCommand, _ args: [UInt8]) {}
    func makeVideo(_ n: AsukaNode) -> NSView { AsukaView(frame: .zero) }
    func setVideoProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func videoCommand(_ n: AsukaNode, _ c: AsukaCommand, _ args: [UInt8]) {}
    func setNativeProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func measureNative(_ n: AsukaNode, _ w: Float, _ h: Float) -> CGSize { .zero }
    func destroyNative(_ n: AsukaNode) {}
    /// The host's own native view kinds (ui.Native's types).
    func registerNatives() {}

    // Hooks (Parts.swift).
    func visualDestroyed(_ n: AsukaNode) {}
    func visualReleased(_ n: AsukaNode) {}
    func visualPlaced(_ n: AsukaNode) {}
    func visualColors(_ n: AsukaNode) {}
    func visualDirection(_ n: AsukaNode, _ changed: Bool) {}
    func visualJSON(_ n: AsukaNode, _ d: inout [String: Any]) {}
    func registerVisualModules() {}
}
