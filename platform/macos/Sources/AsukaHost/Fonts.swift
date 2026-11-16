// The app's fonts: the files in its assets/fonts, registered for the
// process the first time a font is asked for; families and the system
// font's designs.
import AppKit
import CoreText

/// A file's name (no extension) → its PostScript name.
@MainActor private var fontFiles: [String: String]?

/// AppKit's 0-15 weight scale, by Asuka's FontWeight.
private let familyWeights = [5, 2, 3, 4, 5, 6, 8, 9, 10, 11]

@MainActor extension AsukaRuntime {
    func assetFonts() -> [String: String] {
        if let f = fontFiles { return f }
        var out: [String: String] = [:]
        let dir = assets.appendingPathComponent("fonts")
        for name in (try? FileManager.default.contentsOfDirectory(atPath: dir.path)) ?? [] {
            let ext = (name as NSString).pathExtension.lowercased()
            guard ext == "ttf" || ext == "otf" else { continue }
            let u = dir.appendingPathComponent(name)
            CTFontManagerRegisterFontsForURL(u as CFURL, .process, nil)
            if let ds = CTFontManagerCreateFontDescriptorsFromURL(u as CFURL) as? [CTFontDescriptor], let d = ds.first,
               let ps = CTFontDescriptorCopyAttribute(d, kCTFontNameAttribute) as? String {
                out[(name as NSString).deletingPathExtension] = ps
            }
        }
        fontFiles = out
        return out
    }

    /// A font of family at size: an asset file's (Inter-Bold), a family's
    /// (an asset family, Inter, or the system's, Georgia) in weight, or a
    /// generic family (serif, monospace, rounded: the system font's
    /// designs). nil: none such.
    func familyFont(_ family: String, _ size: CGFloat, _ weight: Int32) -> NSFont? {
        if family.contains("://") { return nil }
        if let ps = assetFonts()[family] { return NSFont(name: ps, size: size) }
        let designs: [String: NSFontDescriptor.SystemDesign] = ["serif": .serif, "monospace": .monospaced, "rounded": .rounded, "sans-serif": .default]
        if let d = designs[family] {
            let w = weight > 0 && Int(weight) < asukaWeights.count ? asukaWeights[Int(weight)] : 0
            let sys = NSFont.systemFont(ofSize: size, weight: NSFont.Weight(w))
            guard let desc = sys.fontDescriptor.withDesign(d) else { return sys }
            return NSFont(descriptor: desc, size: size) ?? sys
        }
        let fw = weight > 0 && Int(weight) < familyWeights.count ? familyWeights[Int(weight)] : 5
        return NSFontManager.shared.font(withFamily: family, traits: [], weight: fw, size: size)
    }
}
