// asuka/markdown on iOS and macOS: the paragraphs the core read (md4c)
// as an attributed string, in a text view that isn't editable: one
// selection, links that open, the user's text size.
import Asuka
import Foundation
#if canImport(UIKit)
import UIKit
typealias MDFont = UIFont
typealias MDColor = UIColor
#else
import AppKit
typealias MDFont = NSFont
typealias MDColor = NSColor
#endif

/// The fonts and colors a document is set in.
struct MDTheme {
    var body: MDFont
    var color: MDColor
    var secondary: MDColor
    var codeBackground: MDColor
    var separator: MDColor

    static var current: MDTheme {
        #if canImport(UIKit)
        return MDTheme(body: .preferredFont(forTextStyle: .body), color: .label, secondary: .secondaryLabel,
                       codeBackground: .secondarySystemFill, separator: .separator)
        #else
        return MDTheme(body: .preferredFont(forTextStyle: .body), color: .labelColor, secondary: .secondaryLabelColor,
                       codeBackground: .quaternaryLabelColor, separator: .separatorColor)
        #endif
    }

    func heading(_ level: Int) -> MDFont {
        #if canImport(UIKit)
        let styles: [UIFont.TextStyle] = [.title1, .title2, .title3, .headline, .subheadline, .subheadline]
        #else
        let styles: [NSFont.TextStyle] = [.title1, .title2, .title3, .headline, .subheadline, .subheadline]
        #endif
        return bold(.preferredFont(forTextStyle: styles[min(max(level, 1), 6) - 1]))
    }

    func bold(_ f: MDFont) -> MDFont { traits(f, bold: true, italic: false) }

    func traits(_ f: MDFont, bold: Bool, italic: Bool) -> MDFont {
        #if canImport(UIKit)
        var t = f.fontDescriptor.symbolicTraits
        if bold { t.insert(.traitBold) }
        if italic { t.insert(.traitItalic) }
        guard let d = f.fontDescriptor.withSymbolicTraits(t) else { return f }
        return UIFont(descriptor: d, size: f.pointSize)
        #else
        var t = f.fontDescriptor.symbolicTraits
        if bold { t.insert(.bold) }
        if italic { t.insert(.italic) }
        return NSFont(descriptor: f.fontDescriptor.withSymbolicTraits(t), size: f.pointSize) ?? f
        #endif
    }

    func mono(_ f: MDFont, bold: Bool) -> MDFont {
        .monospacedSystemFont(ofSize: f.pointSize * 0.9, weight: bold ? .semibold : .regular)
    }
}

/// The paragraphs (the core's, md4c's reading) as styled text.
func markdownText(_ paras: [Paragraph], _ th: MDTheme) -> NSAttributedString {
    let out = NSMutableAttributedString()
    var last: NSParagraphStyle?  // the paragraph before's: its newline's
    for (i, p) in paras.enumerated() {
        if let last { out.append(NSAttributedString(string: "\n", attributes: [.font: th.body, .paragraphStyle: last])) }
        let tight = p.tight && i + 1 < paras.count && paras[i + 1].tight
        let spacing: CGFloat = tight ? (p.row ? 2 : 4) : 10
        let style = paragraph(th, indent: p.indent, marker: !p.marker.isEmpty, spacing: spacing, tabs: p.indent > 0 || p.row)
        if p.rule {
            last = paragraph(th, indent: 0, marker: false, spacing: 8, tabs: false)
            out.append(NSAttributedString(string: "\u{2003}", attributes: [
                .font: th.body, .strikethroughStyle: NSUnderlineStyle.single.rawValue, .strikethroughColor: th.separator,
                .paragraphStyle: last!]))
            continue
        }
        last = style
        if !p.marker.isEmpty {
            out.append(NSAttributedString(string: p.marker + "\t", attributes: [.font: th.body, .foregroundColor: th.secondary,
                                                                                 .paragraphStyle: style]))
        }
        for r in p.runs {
            let strong = r.bold || p.heading > 0
            var font = p.heading > 0 ? th.heading(p.heading) : th.body
            var attrs: [NSAttributedString.Key: Any] = [:]
            if p.code || r.code {
                font = th.mono(font, bold: strong)
                attrs[.backgroundColor] = th.codeBackground
            } else {
                font = th.traits(font, bold: strong, italic: r.italic)
            }
            attrs[.font] = font
            attrs[.foregroundColor] = r.quoted ? th.secondary : th.color
            if r.struck { attrs[.strikethroughStyle] = NSUnderlineStyle.single.rawValue }
            if !r.link.isEmpty, let url = URL(string: r.link) { attrs[.link] = url }
            attrs[.paragraphStyle] = style
            out.append(NSAttributedString(string: r.text, attributes: attrs))
        }
    }
    return out
}

/// A paragraph's style: indent steps in (a marker's line starting a step
/// before, the marker in it), its spacing after, tab stops a step apart.
private func paragraph(_ th: MDTheme, indent: Int, marker: Bool, spacing: CGFloat, tabs: Bool) -> NSParagraphStyle {
    let p = NSMutableParagraphStyle()
    let step = th.body.pointSize * 1.5
    p.paragraphSpacing = spacing
    if indent > 0 {
        p.headIndent = step * CGFloat(indent)
        p.firstLineHeadIndent = marker ? step * CGFloat(indent - 1) : p.headIndent
    }
    if tabs {
        p.tabStops = (1...8).map { NSTextTab(textAlignment: .natural, location: step * CGFloat($0)) }
        p.defaultTabInterval = step
    }
    return p
}

#if canImport(UIKit)
final class MarkdownTextView: UITextView {
    var paragraphs: [Paragraph] = [] { didSet { render() } }

    init() {
        super.init(frame: .zero, textContainer: nil)
        isEditable = false
        isSelectable = true
        isScrollEnabled = false
        backgroundColor = .clear
        textContainerInset = .zero
        textContainer.lineFragmentPadding = 0
        dataDetectorTypes = []
        adjustsFontForContentSizeCategory = true
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) isn't used") }

    func render() {
        let t = markdownText(paragraphs, .current)
        if attributedText.isEqual(to: t) { return }  // as it is: its selection kept
        attributedText = t
        invalidateIntrinsicContentSize()
    }

    override func traitCollectionDidChange(_ previous: UITraitCollection?) {
        super.traitCollectionDidChange(previous)
        if previous?.preferredContentSizeCategory != traitCollection.preferredContentSizeCategory { render() }
    }
}

@MainActor final class MarkdownDocument: Document {
    let text = MarkdownTextView()
    var view: AsukaPlatformView { text }
    init(_ events: DocumentEvents) {}
    func setParagraphs(_ value: [Paragraph]) { text.paragraphs = value }
}
#else
final class MarkdownTextView: NSTextView {
    var paragraphs: [Paragraph] = [] { didSet { render() } }

    func render() {
        let t = markdownText(paragraphs, .current)
        guard let storage = textStorage, !storage.isEqual(to: t) else { return }  // as it is: its selection kept
        storage.setAttributedString(t)
    }

    override var isFlipped: Bool { true }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        render()
    }

    /// Its text's height at width.
    func height(for width: CGFloat) -> CGFloat {
        guard let lm = layoutManager, let tc = textContainer else { return 0 }
        let saved = tc.containerSize
        tc.containerSize = CGSize(width: width, height: .greatestFiniteMagnitude)
        lm.ensureLayout(for: tc)
        let h = lm.usedRect(for: tc).height
        tc.containerSize = saved
        return ceil(h)
    }
}

@MainActor final class MarkdownDocument: Document, AsukaSizing {
    let text: MarkdownTextView
    var view: AsukaPlatformView { text }

    init(_ events: DocumentEvents) {
        text = MarkdownTextView(frame: .zero)
        text.isEditable = false
        text.isSelectable = true
        text.drawsBackground = false
        text.textContainerInset = .zero
        text.textContainer?.lineFragmentPadding = 0
        text.textContainer?.widthTracksTextView = true
        text.isVerticallyResizable = false
        text.isHorizontallyResizable = false
    }

    func setParagraphs(_ value: [Paragraph]) { text.paragraphs = value }

    func sizeThatFits(_ size: CGSize) -> CGSize {
        let w = size.width.isFinite && size.width > 0 ? size.width : 10_000
        return CGSize(width: w, height: text.height(for: w))
    }
}
#endif

@MainActor func makeDocument(_ events: DocumentEvents) -> any Document { MarkdownDocument(events) }
