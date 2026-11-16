// std/markdown on iOS and macOS: the paragraphs the core read (md4c)
// as an attributed string, in a text view that isn't editable: one
// selection, links that open, the user's text size. A change writes over
// the text from the first block that differs (MarkdownBlocks).
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

/// Paragraph i of paras as styled text: after i > 0, the paragraph end
/// that parts it from the one before (its spacing above is its own).
func markdownBlock(_ paras: [Paragraph], _ i: Int, _ th: MDTheme) -> NSAttributedString {
    let p = paras[i]
    let out = NSMutableAttributedString()
    let spacing: CGFloat = i == 0 ? 0 : paras[i - 1].tight && p.tight ? (p.row ? 2 : 4) : 10
    if p.rule {
        let style = paragraph(th, indent: 0, marker: false, spacing: spacing + 4, tabs: false)
        out.append(NSAttributedString(string: i > 0 ? "\n\u{2003}" : "\u{2003}", attributes: [
            .font: th.body, .strikethroughStyle: NSUnderlineStyle.single.rawValue, .strikethroughColor: th.separator, .paragraphStyle: style]))
        return out
    }
    let style = paragraph(th, indent: p.indent, marker: !p.marker.isEmpty, spacing: spacing, tabs: p.indent > 0 || p.row)
    if i > 0 { out.append(NSAttributedString(string: "\n", attributes: [.font: th.body, .paragraphStyle: style])) }
    if !p.marker.isEmpty {
        out.append(NSAttributedString(string: p.marker + "\t", attributes: [.font: th.body, .foregroundColor: th.secondary, .paragraphStyle: style]))
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
    return out
}

/// What a text view shows of the paragraphs, a block each, kept while it
/// says the same: a change (a reply streaming in) writes over the text
/// from the first block that differs to its end, so the text before it
/// keeps its layout and its selection.
@MainActor final class MarkdownBlocks {
    private(set) var blocks: [NSAttributedString] = []

    /// Shows paras in storage (all: every block again, as the theme
    /// changed): where the text written over starts, or nil when nothing
    /// changed.
    @discardableResult
    func show(_ paras: [Paragraph], in storage: NSTextStorage, _ th: MDTheme, all: Bool = false) -> Int? {
        let next = paras.indices.map { markdownBlock(paras, $0, th) }
        var keep = 0
        if !all {
            while keep < blocks.count && keep < next.count && blocks[keep].isEqual(to: next[keep]) { keep += 1 }
            if keep == blocks.count && keep == next.count { return nil }
        }
        let start = blocks[..<keep].reduce(0) { $0 + $1.length }
        let tail = NSMutableAttributedString()
        for b in next[keep...] { tail.append(b) }
        storage.beginEditing()
        storage.replaceCharacters(in: NSRange(location: start, length: storage.length - start), with: tail)
        storage.endEditing()
        blocks = next
        return start
    }
}

/// selection is sel as it is once the text from start on is written over:
/// what of it was before start.
func keptSelection(_ sel: NSRange, before start: Int) -> NSRange {
    let from = min(sel.location, start)
    return NSRange(location: from, length: min(NSMaxRange(sel), start) - from)
}

/// A paragraph's style: indent steps in (a marker's line starting a step
/// before, the marker in it), its spacing above, tab stops a step apart.
private func paragraph(_ th: MDTheme, indent: Int, marker: Bool, spacing: CGFloat, tabs: Bool) -> NSParagraphStyle {
    let p = NSMutableParagraphStyle()
    let step = th.body.pointSize * 1.5
    p.paragraphSpacingBefore = spacing
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
    let shown = MarkdownBlocks()
    var paragraphs: [Paragraph] = [] { didSet { render(all: false) } }

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

    func render(all: Bool) {
        let sel = selectedRange
        guard let start = shown.show(paragraphs, in: textStorage, .current, all: all) else { return }
        selectedRange = keptSelection(sel, before: start)
        invalidateIntrinsicContentSize()
    }

    override func traitCollectionDidChange(_ previous: UITraitCollection?) {
        super.traitCollectionDidChange(previous)
        if previous?.preferredContentSizeCategory != traitCollection.preferredContentSizeCategory { render(all: true) }
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
    let shown = MarkdownBlocks()
    var paragraphs: [Paragraph] = [] { didSet { render(all: false) } }

    func render(all: Bool) {
        guard let storage = textStorage else { return }
        let sel = selectedRange()
        guard let start = shown.show(paragraphs, in: storage, .current, all: all) else { return }
        setSelectedRange(keptSelection(sel, before: start))
    }

    override var isFlipped: Bool { true }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        render(all: true)
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
