// MarkdownViewTest.swift: the iOS and macOS view (swift/Markdown.swift's
// MarkdownTextView: a real UITextView, NSTextView) given a reply streamed
// in frame by frame (stream.json, its path the first argument): each frame
// writes over the text storage once at most (its edits caught as they're
// processed), from where the first block that changed starts or after,
// the text before it as it was; a selection in the heading survives; and
// at the end the storage is what a view given the whole reply has,
// attribute by attribute. host/darwin's TestStdSwiftViews builds it with
// the package's Swift and runs it on macOS and in the iOS simulator.
import Foundation
#if canImport(UIKit)
import UIKit
#else
import AppKit
#endif

@MainActor var failed = 0

@MainActor func check(_ ok: Bool, _ what: @autoclosure () -> String, line: Int = #line) {
    if !ok {
        print("MarkdownViewTest.swift:\(line): \(what())")
        failed += 1
    }
}

func frames(_ path: String) -> [[Paragraph]] {
    guard let data = FileManager.default.contents(atPath: path),
          let doc = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
          let frames = doc["frames"] as? [[[String: Any]]] else {
        print("no frames in \(path)")
        exit(1)
    }
    return frames.map { frame in
        frame.map { p in
            Paragraph(runs: (p["runs"] as? [[String: Any]] ?? []).map { r in
                Run(text: r["text"] as? String ?? "", bold: r["bold"] as? Bool ?? false, italic: r["italic"] as? Bool ?? false,
                    code: r["code"] as? Bool ?? false, struck: r["struck"] as? Bool ?? false, quoted: r["quoted"] as? Bool ?? false,
                    link: r["link"] as? String ?? "")
            }, heading: p["heading"] as? Int ?? 0, indent: p["indent"] as? Int ?? 0, marker: p["marker"] as? String ?? "",
                      code: p["code"] as? Bool ?? false, rule: p["rule"] as? Bool ?? false, row: p["row"] as? Bool ?? false,
                      tight: p["tight"] as? Bool ?? false)
        }
    }
}

func same(_ a: Run, _ b: Run) -> Bool {
    a.text == b.text && a.bold == b.bold && a.italic == b.italic && a.code == b.code && a.struck == b.struck && a.quoted == b.quoted && a.link == b.link
}

func same(_ a: Paragraph, _ b: Paragraph) -> Bool {
    a.heading == b.heading && a.indent == b.indent && a.marker == b.marker && a.code == b.code && a.rule == b.rule && a.row == b.row &&
        a.tight == b.tight && a.runs.count == b.runs.count && zip(a.runs, b.runs).allSatisfy { same($0, $1) }
}

/// The first paragraph of b that a doesn't have as it is.
func firstChange(_ a: [Paragraph], _ b: [Paragraph]) -> Int {
    var i = 0
    while i < a.count && i < b.count && same(a[i], b[i]) { i += 1 }
    return i
}

@MainActor func makeView() -> MarkdownTextView {
    #if canImport(UIKit)
    let v = MarkdownTextView()
    v.frame = CGRect(x: 0, y: 0, width: 400, height: 2000)
    #else
    let v = MarkdownTextView(frame: NSRect(x: 0, y: 0, width: 400, height: 2000))
    v.isEditable = false
    v.isSelectable = true
    #endif
    return v
}

@MainActor func storage(_ v: MarkdownTextView) -> NSTextStorage {
    #if canImport(UIKit)
    return v.textStorage
    #else
    return v.textStorage!
    #endif
}

@MainActor func selection(_ v: MarkdownTextView) -> NSRange {
    #if canImport(UIKit)
    return v.selectedRange
    #else
    return v.selectedRange()
    #endif
}

@MainActor func select(_ v: MarkdownTextView, _ r: NSRange) {
    #if canImport(UIKit)
    v.selectedRange = r
    #else
    v.setSelectedRange(r)
    #endif
}

@main @MainActor enum MarkdownViewTest {
    static func main() {
        let stream = frames(CommandLine.arguments[1])
        check(stream.count > 20, "\(stream.count) frames")
        let v = makeView()
        let text = storage(v)
        var edits: [NSRange] = []
        let watch = NotificationCenter.default.addObserver(forName: NSTextStorage.willProcessEditingNotification, object: text, queue: nil) { _ in
            MainActor.assumeIsolated {
                if text.editedMask.contains(.editedCharacters) { edits.append(text.editedRange) }
            }
        }
        var prev: [Paragraph] = []
        var replaced = 0, shown = 0
        var selected = false
        for (f, paras) in stream.enumerated() {
            let first = firstChange(prev, paras)
            let from = v.shown.blocks.prefix(first).reduce(0) { $0 + $1.length }
            let before = text.string as NSString
            edits = []
            v.paragraphs = paras
            let now = text.string as NSString
            check(v.shown.blocks.count == paras.count, "frame \(f): \(v.shown.blocks.count) blocks for \(paras.count) paragraphs")
            check(now.length == v.shown.blocks.reduce(0) { $0 + $1.length }, "frame \(f): the storage isn't its blocks")
            check(edits.count <= 1, "frame \(f): \(edits.count) edits")
            if first == prev.count && first == paras.count { check(edits.isEmpty, "frame \(f): nothing changed, \(edits.count) edits") }
            if let e = edits.first {
                check(e.location >= from && NSMaxRange(e) == now.length,
                      "frame \(f): wrote [\(e.location), \(NSMaxRange(e))), want from \(from) (paragraph \(first)'s start) or after to \(now.length)")
                check(before.length >= e.location && before.substring(to: e.location) == now.substring(to: e.location),
                      "frame \(f): the text before the change changed")
                replaced += now.length - e.location
            }
            shown += now.length
            prev = paras
            // Once the heading is whole and the next block has begun, part of
            // it is selected, as a reader would while the rest streams in.
            if !selected && paras.count >= 2 {
                selected = true
                select(v, NSRange(location: 2, length: 4))
            }
        }
        NotificationCenter.default.removeObserver(watch)
        let sel = selection(v)
        check(selected && sel == NSRange(location: 2, length: 4), "the selection in the heading: \(sel)")
        check(replaced * 4 < shown, "replaced \(replaced) characters of the \(shown) shown frame by frame")

        // At the end, what a view given it all at once has.
        let whole = makeView()
        whole.paragraphs = stream[stream.count - 1]
        check(storage(whole).isEqual(to: text), "the storage streamed isn't the whole's:\n\(text)\n---\n\(storage(whole))")
        check(text.string.hasPrefix("Moving the cache"), "the text: \(text.string)")
        #if canImport(UIKit)
        let platform = "ios"
        #else
        let platform = "macos"
        #endif
        print("markdown (\(platform)): \(stream.count) frames, \(replaced) characters replaced of \(shown) shown")
        exit(failed == 0 ? 0 : 1)
    }
}
