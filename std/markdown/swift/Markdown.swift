// asuka/markdown on iOS and macOS: Apple's Markdown (AttributedString,
// its full syntax) laid out as styled paragraphs, from each run's
// presentation intent (headings, lists, quotes, code blocks, tables), in
// a text view that isn't editable: one selection, links that open, the
// user's text size. Raw HTML stays text.
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

/// The document as styled paragraphs.
func markdownText(_ md: String, _ th: MDTheme) -> NSAttributedString {
    let out = NSMutableAttributedString()
    let opts = AttributedString.MarkdownParsingOptions(allowsExtendedAttributes: false, interpretedSyntax: .full,
                                                       failurePolicy: .returnPartiallyParsedIfPossible)
    guard let doc = try? AttributedString(markdown: md, options: opts) else {
        return NSAttributedString(string: md, attributes: [.font: th.body, .foregroundColor: th.color])
    }
    var block = -1         // the identity of the block the last run was in
    var items = Set<Int>() // the list items whose marker is out
    var row = -1
    for run in doc.runs {
        let text = String(doc[run.range].characters)
        let comps = run.presentationIntent?.components ?? []
        let leaf = comps.first?.identity ?? 0
        var depth = 0, quote = false, code = false, heading = 0, cell = -1, header = false, rule = false
        var marker = ""
        for c in comps {
            switch c.kind {
            case .header(let level): heading = level
            case .codeBlock: code = true
            case .blockQuote: quote = true
            case .thematicBreak: rule = true
            case .tableCell(let column): cell = column
            case .tableHeaderRow: header = true
            case .listItem(let ordinal):
                depth += 1
                if !items.contains(c.identity) && marker.isEmpty {
                    items.insert(c.identity)
                    marker = isOrdered(comps, after: c) ? "\(ordinal).\t" : "•\t"
                }
            default: break
            }
        }
        if leaf != block {
            // A new block: a new paragraph, or a table's next cell.
            let rowID = comps.first { if case .tableRow = $0.kind { return true }; if case .tableHeaderRow = $0.kind { return true }; return false }?.identity ?? -1
            if out.length > 0 {
                out.append(NSAttributedString(string: cell > 0 && rowID == row ? "\t" : "\n"))
            }
            row = rowID
            block = leaf
            if rule {
                out.append(NSAttributedString(string: "\u{2003}", attributes: [
                    .font: th.body, .strikethroughStyle: NSUnderlineStyle.single.rawValue, .strikethroughColor: th.separator,
                    .paragraphStyle: paragraph(th, indent: 0, spacing: 8, tabs: false)]))
                continue
            }
            if !marker.isEmpty {
                out.append(NSAttributedString(string: marker, attributes: [.font: th.body, .foregroundColor: th.secondary,
                                                                           .paragraphStyle: paragraph(th, indent: depth, spacing: 4, tabs: true)]))
            }
        }
        if rule { continue }
        // The run's own style.
        let inline = run.inlinePresentationIntent ?? []
        let strong = inline.contains(.stronglyEmphasized) || header || heading > 0
        let em = inline.contains(.emphasized)
        var font = heading > 0 ? th.heading(heading) : th.body
        var attrs: [NSAttributedString.Key: Any] = [:]
        if code || inline.contains(.code) {
            font = th.mono(font, bold: strong)
            attrs[.backgroundColor] = th.codeBackground
        } else {
            font = th.traits(font, bold: strong, italic: em)
        }
        attrs[.font] = font
        attrs[.foregroundColor] = quote ? th.secondary : th.color
        if inline.contains(.strikethrough) { attrs[.strikethroughStyle] = NSUnderlineStyle.single.rawValue }
        if let url = run.link { attrs[.link] = url }
        let spacing: CGFloat = heading > 0 ? 10 : depth > 0 ? 4 : cell >= 0 ? 2 : 10
        attrs[.paragraphStyle] = paragraph(th, indent: depth + (quote ? 1 : 0), spacing: spacing, tabs: depth > 0 || cell >= 0)
        out.append(NSAttributedString(string: code ? text.trimmingTrailingNewline() : text, attributes: attrs))
    }
    return out
}

/// Whether the list holding item is an ordered one: the list right
/// outside it in comps.
private func isOrdered(_ comps: [PresentationIntent.IntentType], after item: PresentationIntent.IntentType) -> Bool {
    guard let i = comps.firstIndex(where: { $0.identity == item.identity }), i + 1 < comps.count else { return false }
    if case .orderedList = comps[i + 1].kind { return true }
    return false
}

private func paragraph(_ th: MDTheme, indent: Int, spacing: CGFloat, tabs: Bool) -> NSParagraphStyle {
    let p = NSMutableParagraphStyle()
    let step = th.body.pointSize * 1.5
    p.paragraphSpacing = spacing
    if indent > 0 {
        p.firstLineHeadIndent = step * CGFloat(indent - 1)
        p.headIndent = step * CGFloat(indent)
    }
    if tabs {
        p.tabStops = (1...8).map { NSTextTab(textAlignment: .natural, location: step * CGFloat($0)) }
        p.defaultTabInterval = step
    }
    return p
}

private extension String {
    func trimmingTrailingNewline() -> String { hasSuffix("\n") ? String(dropLast()) : self }
}

#if canImport(UIKit)
final class MarkdownTextView: UITextView {
    var markdown = "" { didSet { if markdown != oldValue { render() } } }

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
        attributedText = markdownText(markdown, .current)
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
    func setText(_ value: String) { text.markdown = value }
}
#else
final class MarkdownTextView: NSTextView {
    var markdown = "" { didSet { if markdown != oldValue { render() } } }

    func render() {
        textStorage?.setAttributedString(markdownText(markdown, .current))
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

    func setText(_ value: String) { text.markdown = value }

    func sizeThatFits(_ size: CGSize) -> CGSize {
        let w = size.width.isFinite && size.width > 0 ? size.width : 10_000
        return CGSize(width: w, height: text.height(for: w))
    }
}
#endif

@MainActor func makeDocument(_ events: DocumentEvents) -> any Document { MarkdownDocument(events) }
