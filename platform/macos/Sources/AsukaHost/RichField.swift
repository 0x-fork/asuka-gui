// A Rich TextField. Ports host/darwin's richfield.go.
//
// Its text view, made rich, from and to the core's styled text (RichText's
// Content encoding). Bold, italic, underline, strikethrough, monospaced and
// links are what the text has (the Format menu's ⌘B, ⌘I, ⌘U, and the Format
// and SetLink commands change them); each run's style as the core sent it
// rides along (an index into the field's styles, in an attribute of its
// own), so what the user didn't change comes back as it was sent.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

extension NSAttributedString.Key {
    /// The index of a run's style in its field's styles.
    static let asukaFieldStyle = NSAttributedString.Key("asuka.fieldStyle")
}

private let semibold: Int32 = 6 // ops.FontWeightSemibold
private let bold: Int32 = 7 // ops.FontWeightBold
private let fixedPitchMask: UInt = 0x400 // NSFixedPitchFontMask

/// TextFormat bits (ops.TextFormat): 1 bold, 2 italic, 4 underline, 8
/// strikethrough, 16 monospaced.
extension AsukaRichStyle {
    var format: UInt8 {
        var f: UInt8 = 0
        if weight >= semibold { f |= 1 }
        if flags & 1 != 0 { f |= 2 }
        if flags & 4 != 0 { f |= 4 }
        if flags & 8 != 0 { f |= 8 }
        if flags & 2 != 0 { f |= 16 }
        return f
    }

    /// s with the TextFormat bits on or off.
    func formatted(_ bits: UInt8, _ on: Bool) -> AsukaRichStyle {
        var s = self
        if bits & 1 != 0 {
            if on { s.weight = max(s.weight, bold) } else if s.weight >= semibold { s.weight = 0 }
        }
        for (bit, flag) in [(UInt8(2), UInt8(1)), (16, 2), (4, 4), (8, 8)] where bits & bit != 0 {
            if on { s.flags |= flag } else { s.flags &= ~flag }
        }
        return s
    }
}

/// A Rich field's state.
@MainActor final class AsukaRichField {
    /// The styles runs have had: asukaFieldStyle indexes them.
    var styles: [AsukaRichStyle] = []
    var setting = false
    var formats: UInt8 = 0
    var link = ""
    /// Formats and link have been reported.
    var known = false

    /// s's place in the field's styles, added if it's new.
    func index(_ s: AsukaRichStyle) -> Int {
        if let i = styles.firstIndex(of: s) { return i }
        styles.append(s)
        return styles.count - 1
    }
}

@MainActor extension AsukaRuntime {
    /// The text attributes of a style in n.
    func fieldAttrs(_ n: AsukaNode, _ s: AsukaRichStyle) -> [NSAttributedString.Key: Any] {
        let ts = s.textStyle != 0 ? s.textStyle : n.textStyle
        let size = s.size != 0 ? s.size : n.fontSize
        let weight = s.weight != 0 ? s.weight : n.fontWeight
        var f = font(style: ts, weight: weight, size: size, family: n.fontFamily)
        let fm = NSFontManager.shared
        if s.mono {
            let w = weight > 0 && Int(weight) < asukaWeights.count ? asukaWeights[Int(weight)] : 0
            f = NSFont.monospacedSystemFont(ofSize: f.pointSize, weight: NSFont.Weight(w))
        }
        if s.weight >= semibold { f = fm.convert(f, toHaveTrait: .boldFontMask) }
        if s.italic { f = fm.convert(f, toHaveTrait: .italicFontMask) }
        var a: [NSAttributedString.Key: Any] = [.font: f]
        a[.foregroundColor] = asukaColor(s.color) ?? asukaColor(n.color) ?? NSColor.textColor
        if s.background != 0, let bg = asukaColor(s.background) { a[.backgroundColor] = bg }
        if s.underline { a[.underlineStyle] = NSUnderlineStyle.single.rawValue }
        if s.strike { a[.strikethroughStyle] = NSUnderlineStyle.single.rawValue }
        if !s.link.isEmpty { a[.link] = s.link }
        a[.asukaFieldStyle] = n.richField?.index(s) ?? 0
        return a
    }

    private func traits(_ f: NSFont?) -> UInt {
        guard let f else { return 0 }
        var t = NSFontManager.shared.traits(of: f).rawValue
        if f.isFixedPitch { t |= fixedPitchMask }
        return t
    }

    /// The style of text with these attributes: the core's as it was sent,
    /// with what the user changed.
    func styleOf(_ n: AsukaNode, _ attrs: [NSAttributedString.Key: Any]) -> AsukaRichStyle {
        var s = AsukaRichStyle()
        if let k = attrs[.asukaFieldStyle] as? Int, let f = n.richField, k >= 0, k < f.styles.count { s = f.styles[k] }
        let t = traits(attrs[.font] as? NSFont)
        // What the field's own font has (a headline's bold) isn't the run's:
        // the run keeps what it was sent with for it.
        let base = traits(font(n))
        let boldMask = NSFontTraitMask.boldFontMask.rawValue, italicMask = NSFontTraitMask.italicFontMask.rawValue
        if base & boldMask == 0 {
            if t & boldMask != 0 && s.weight < semibold { s.weight = bold } else if t & boldMask == 0 && s.weight >= semibold { s.weight = 0 }
        }
        var flags = s.flags & 3 // kept where the field's font decides
        if base & italicMask == 0 {
            flags &= ~1
            if t & italicMask != 0 { flags |= 1 }
        }
        if base & fixedPitchMask == 0 {
            flags &= ~2
            if t & fixedPitchMask != 0 { flags |= 2 }
        }
        if let u = attrs[.underlineStyle] as? Int, u != 0 { flags |= 4 }
        if let u = attrs[.strikethroughStyle] as? Int, u != 0 { flags |= 8 }
        s.flags = flags
        s.link = attrs[.link].map { asukaLinkString($0) } ?? ""
        return s
    }

    /// The Rich prop: the field's text, styled.
    func setRichField(_ n: AsukaNode, _ b: [UInt8]) {
        if n.textView == nil { makeMultiline(n) }
        if n.richField == nil { n.richField = AsukaRichField() }
        guard let tv = n.textView, let f = n.richField else { return }
        AsukaTextDelegate.shared.start()
        tv.isRichText = true
        tv.importsGraphics = false
        let c = AsukaRichContent(b)
        let str = NSMutableAttributedString()
        var last = AsukaRichStyle()
        for (p, runs) in c.paragraphs.enumerated() {
            if p > 0 { str.append(NSAttributedString(string: "\n", attributes: fieldAttrs(n, last))) }
            for run in runs {
                guard run.style < c.styles.count, !c.styles[run.style].inline else { continue }
                last = c.styles[run.style]
                str.append(NSAttributedString(string: run.text, attributes: fieldAttrs(n, last)))
            }
        }
        let sel = tv.selectedRange()
        f.setting = true
        tv.textStorage?.setAttributedString(str)
        let length = str.length
        let loc = min(sel.location, length)
        tv.setSelectedRange(NSRange(location: loc, length: min(sel.length, length - loc)))
        if length == 0 { tv.typingAttributes = fieldAttrs(n, AsukaRichStyle()) }
        f.setting = false
        n.text = tv.string
        reportFormats(n)
    }

    /// The field's styled text as the core encodes it: styles in the order
    /// runs first use them, a paragraph per line, runs of one style merged.
    func encodeRichField(_ n: AsukaNode) -> [UInt8] {
        guard let ts = n.textView?.textStorage else { return [] }
        let text = ts.string as NSString
        var styles: [AsukaRichStyle] = []
        var paras: [[(style: Int, text: String)]] = [[]]
        func add(_ s: AsukaRichStyle, _ range: NSRange) {
            var at = range.location
            let end = range.location + range.length
            while at < end {
                let nl = text.range(of: "\n", options: [], range: NSRange(location: at, length: end - at))
                let partEnd = nl.location == NSNotFound ? end : nl.location
                if partEnd > at {
                    let part = text.substring(with: NSRange(location: at, length: partEnd - at))
                    let k: Int
                    if let i = styles.firstIndex(of: s) { k = i } else { styles.append(s); k = styles.count - 1 }
                    let p = paras.count - 1
                    if let l = paras[p].last, l.style == k {
                        paras[p][paras[p].count - 1].text += part
                    } else {
                        paras[p].append((k, part))
                    }
                }
                if nl.location == NSNotFound { break }
                paras.append([])
                at = nl.location + 1
            }
        }
        var i = 0
        while i < text.length {
            var r = NSRange(location: 0, length: 0)
            let attrs = ts.attributes(at: i, effectiveRange: &r)
            var end = min(r.location + r.length, text.length)
            if end <= i { end = i + 1 }
            add(styleOf(n, attrs), NSRange(location: i, length: end - i))
            i = end
        }
        var w = AsukaByteWriter()
        w.u32(UInt32(styles.count))
        for s in styles {
            w.u8(UInt8(clamping: s.textStyle))
            w.f32(s.size)
            w.u8(UInt8(clamping: s.weight))
            w.u8(s.flags)
            w.u64(s.color)
            w.u64(s.background)
            w.string(s.link)
        }
        w.u32(UInt32(paras.count))
        for p in paras {
            w.u32(UInt32(p.count))
            for r in p {
                w.u32(UInt32(r.style))
                w.string(r.text)
            }
        }
        return w.bytes
    }

    /// Reports the user's change of the text or its styles.
    func richFieldChanged(_ n: AsukaNode) {
        guard let f = n.richField, !f.setting else { return }
        let b = encodeRichField(n)
        emit(n, .richChanged) { AsukaEvents.richChanged($0, b) }
        reportFormats(n)
    }

    /// The styles at the cursor (what's typed next: the typing attributes)
    /// or that all the selection has, and its link.
    func formatsOf(_ n: AsukaNode) -> (UInt8, String) {
        guard let tv = n.textView, let ts = tv.textStorage else { return (0, "") }
        let sel = tv.selectedRange()
        if sel.length == 0 {
            let s = styleOf(n, tv.typingAttributes)
            return (s.format, s.link)
        }
        var all: UInt8 = 31
        var link = "", first = true
        var i = sel.location
        while i < sel.location + sel.length && i < ts.length {
            var r = NSRange(location: 0, length: 0)
            let s = styleOf(n, ts.attributes(at: i, effectiveRange: &r))
            all &= s.format
            if first {
                link = s.link
                first = false
            } else if link != s.link {
                link = ""
            }
            i = max(r.location + r.length, i + 1)
        }
        return (all, link)
    }

    /// Sends Formats if they changed.
    func reportFormats(_ n: AsukaNode) {
        guard let f = n.richField, n.textView != nil, n.listening(.formats) else { return }
        let (fm, link) = formatsOf(n)
        if f.known && fm == f.formats && link == f.link { return }
        f.known = true
        f.formats = fm
        f.link = link
        var w = AsukaByteWriter()
        w.u8(fm)
        w.string(link)
        let b = w.bytes
        emit(n, .formats) { AsukaEvents.formats($0, b) }
    }

    /// Changes each run of the selection through f, undoably (the text
    /// view's own undo, as its Format menu's changes are); at the cursor,
    /// the typing attributes.
    func restyleField(_ n: AsukaNode, _ wholeLink: Bool, _ f: (AsukaRichStyle) -> AsukaRichStyle) {
        guard let tv = n.textView, let ts = tv.textStorage else { return }
        var sel = tv.selectedRange()
        if sel.length == 0 && wholeLink {
            // The link the cursor is in.
            let length = ts.length
            guard length > 0 else { return }
            let at = sel.location > 0 ? min(sel.location - 1, length - 1) : 0
            var r = NSRange(location: 0, length: 0)
            guard ts.attribute(.link, at: at, effectiveRange: &r) != nil else { return }
            sel = r
        }
        if sel.length == 0 {
            tv.typingAttributes = fieldAttrs(n, f(styleOf(n, tv.typingAttributes)))
            reportFormats(n)
            return
        }
        guard tv.shouldChangeText(in: sel, replacementString: nil) else { return }
        ts.beginEditing()
        var i = sel.location
        let end = sel.location + sel.length
        while i < end && i < ts.length {
            var r = NSRange(location: 0, length: 0)
            let attrs = ts.attributes(at: i, effectiveRange: &r)
            let e = min(r.location + r.length, end)
            ts.setAttributes(fieldAttrs(n, f(styleOf(n, attrs))), range: NSRange(location: i, length: e - i))
            i = max(e, i + 1)
        }
        ts.endEditing()
        tv.didChangeText() // the undo, and textDidChange: reported there
    }

    /// The Format command: u8 TextFormat bits, u8 how (0 off, 1 on, 2
    /// toggled: on unless all the selection has them).
    func formatField(_ n: AsukaNode, _ b: [UInt8]) {
        guard n.richField != nil, n.textView != nil, b.count >= 2 else { return }
        let bits = b[0], how = b[1]
        let (has, _) = formatsOf(n)
        var on = how == 1
        if how == 2 { on = has & bits != bits }
        restyleField(n, false) { $0.formatted(bits, on) }
    }

    /// The SetLink command: the selection (or the link the cursor is in)
    /// links to url ("" for none).
    func setFieldLink(_ n: AsukaNode, _ url: String) {
        guard n.richField != nil, n.textView != nil else { return }
        restyleField(n, true) { s in
            var s = s
            s.link = url
            return s
        }
    }
}
