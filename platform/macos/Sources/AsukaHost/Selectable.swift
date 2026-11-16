// A Selectable's selection, across its RichTexts. AppKit keeps one text
// view's selection at a time (and draws it as that view's own), so the
// selection is the host's, as on Linux: a drag on the Selectable (on a text
// in it, or between them) maps the pointer to a text and a character
// through each text view's layout, each text between the ends in reading
// order (the core's tree order) has its part, drawn in the selection's
// color (the layout manager's temporary attributes, which the text keeps
// through its own drawing). A double click selects words, a triple click
// paragraphs, Shift+click extends; ⌘A selects all its texts and ⌘C (the
// Edit menu's Copy) copies the parts, a paragraph each, without the inline
// components. A press on a link, a control or a pressable view is theirs.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

/// A place in a Selectable's texts: the text's index in reading order, a
/// UTF-16 offset in it.
struct AsukaTextPlace: Comparable {
    var text: Int
    var char: Int
    static func < (a: AsukaTextPlace, b: AsukaTextPlace) -> Bool { a.text != b.text ? a.text < b.text : a.char < b.char }
}

/// A Selectable's selection: where it started, where it is, and by what it
/// goes (characters, words, paragraphs).
@MainActor final class AsukaSelectableState {
    var anchor = AsukaTextPlace(text: 0, char: 0)
    var focus = AsukaTextPlace(text: 0, char: 0)
    var granularity = NSSelectionGranularity.selectByCharacter
}

@MainActor private var selections: [UInt32: AsukaSelectableState] = [:]

@MainActor extension AsukaRuntime {
    /// The Selectable n is in (n itself if it's one).
    func selectableOf(_ n: AsukaNode) -> AsukaNode? {
        var x: AsukaNode? = n
        while let c = x {
            if c.kind == .selectable { return c }
            x = c.parent
        }
        return nil
    }

    /// A Selectable's RichTexts, in reading order.
    func selectableTexts(_ s: AsukaNode) -> [AsukaNode] {
        var out: [AsukaNode] = []
        func walk(_ n: AsukaNode) {
            if n.rich != nil { out.append(n) }
            for c in n.children { walk(c) }
        }
        walk(s)
        return out
    }

    private func length(_ t: AsukaNode) -> Int { t.rich?.view.textStorage?.length ?? 0 }

    /// The place under a point of the window: in the text there, else at
    /// the end of the last text above it, beside it the nearest of its
    /// lines.
    func textPlace(_ s: AsukaNode, _ inWindow: CGPoint) -> AsukaTextPlace {
        let texts = selectableTexts(s)
        guard let sv = s.view else { return AsukaTextPlace(text: 0, char: 0) }
        let p = sv.convert(inWindow, from: nil)
        var best = AsukaTextPlace(text: 0, char: 0)
        for (i, t) in texts.enumerated() {
            guard let v = t.rich?.view, !v.isHiddenOrHasHiddenAncestor else { continue }
            let f = v.convert(v.bounds, to: sv)
            if f.contains(p) { return AsukaTextPlace(text: i, char: v.characterIndexForInsertion(at: v.convert(p, from: sv))) }
            if p.y >= f.maxY {
                best = AsukaTextPlace(text: i, char: length(t))
            } else if p.y >= f.minY {
                let q = CGPoint(x: min(max(p.x, f.minX), f.maxX - 0.5), y: p.y)
                best = AsukaTextPlace(text: i, char: v.characterIndexForInsertion(at: v.convert(q, from: sv)))
            }
        }
        return best
    }

    /// The selection's ends in order, widened to words or paragraphs as it
    /// goes by them.
    func selectionEnds(_ s: AsukaNode, _ st: AsukaSelectableState) -> (AsukaTextPlace, AsukaTextPlace) {
        var lo = min(st.anchor, st.focus), hi = max(st.anchor, st.focus)
        let texts = selectableTexts(s)
        lo.char = min(lo.char, lo.text < texts.count ? length(texts[lo.text]) : 0)
        hi.char = min(hi.char, hi.text < texts.count ? length(texts[hi.text]) : 0)
        if st.granularity != .selectByCharacter {
            if lo.text < texts.count, let v = texts[lo.text].rich?.view {
                lo.char = v.selectionRange(forProposedRange: NSRange(location: lo.char, length: 0), granularity: st.granularity).location
            }
            if hi.text < texts.count, let v = texts[hi.text].rich?.view {
                let r = v.selectionRange(forProposedRange: NSRange(location: hi.char, length: 0), granularity: st.granularity)
                hi.char = r.location + r.length
            }
        }
        return (lo, hi)
    }

    /// Each text's part of the selection.
    func selectionRanges(_ s: AsukaNode) -> [NSRange] {
        let texts = selectableTexts(s)
        guard let st = selections[s.id] else { return texts.map { _ in NSRange(location: 0, length: 0) } }
        let (lo, hi) = selectionEnds(s, st)
        return texts.enumerated().map { i, t in
            guard i >= lo.text && i <= hi.text else { return NSRange(location: 0, length: 0) }
            let a = i == lo.text ? lo.char : 0, b = i == hi.text ? hi.char : length(t)
            return NSRange(location: a, length: max(b - a, 0))
        }
    }

    /// A text's part of its Selectable's selection.
    func selectionOf(_ n: AsukaNode) -> NSRange? {
        guard let s = selectableOf(n), selections[s.id] != nil else { return nil }
        let texts = selectableTexts(s)
        guard let i = texts.firstIndex(where: { $0 === n }) else { return nil }
        return selectionRanges(s)[i]
    }

    /// Draws the selection in each text: the selected text's background
    /// (unemphasized while the window isn't key).
    func showSelection(_ s: AsukaNode) {
        let texts = selectableTexts(s)
        let ranges = selectionRanges(s)
        let key = s.view?.window?.isKeyWindow ?? true
        let color = key ? NSColor.selectedTextBackgroundColor : NSColor.unemphasizedSelectedTextBackgroundColor
        for (i, t) in texts.enumerated() {
            guard let v = t.rich?.view, let lm = v.layoutManager else { continue }
            let all = NSRange(location: 0, length: length(t))
            lm.removeTemporaryAttribute(.backgroundColor, forCharacterRange: all)
            if i < ranges.count, ranges[i].length > 0 { lm.addTemporaryAttribute(.backgroundColor, value: color, forCharacterRange: ranges[i]) }
            v.needsDisplay = true
        }
    }

    /// A press in a Selectable (n: the text it's on, or the Selectable's
    /// view or a view in it): the selection follows the drag until the
    /// button goes up. False: it isn't the Selectable's.
    func selectableTrack(_ n: AsukaNode, _ e: NSEvent) -> Bool {
        guard let s = selectableOf(n), let w = s.view?.window else { return false }
        let st = selections[s.id] ?? AsukaSelectableState()
        selections[s.id] = st
        let at = textPlace(s, e.locationInWindow)
        if !e.modifierFlags.contains(.shift) { st.anchor = at }
        st.focus = at
        switch e.clickCount {
        case 2: st.granularity = .selectByWord
        case 3...: st.granularity = .selectByParagraph
        default: st.granularity = .selectByCharacter
        }
        // A text of it takes the keyboard's focus, for ⌘C and ⌘A.
        let texts = selectableTexts(s)
        if at.text < texts.count, let v = texts[at.text].rich?.view { w.makeFirstResponder(v) }
        showSelection(s)
        while let ev = w.nextEvent(matching: [.leftMouseDragged, .leftMouseUp]) {
            st.focus = textPlace(s, ev.locationInWindow)
            showSelection(s)
            if ev.type == .leftMouseUp { break }
        }
        return true
    }

    /// A press on a view (not a text) in a Selectable, between its texts:
    /// the Selectable's, unless a pressable view in it takes it.
    func selectableMouseDown(_ v: NSView, _ e: NSEvent) -> Bool {
        var x = nodeAt(v)
        while let n = x {
            if n.kind == .selectable { return selectableTrack(n, e) }
            if n.listening(.press) || n.listening(.longPress) || n.listening(.pressed) || n.menu != nil { return false }
            x = n.parent
        }
        return false
    }

    /// What a view's mouse down is for the list and text parts: a row of a
    /// selecting list selected, a Selectable's selection begun. False:
    /// neither (the view's press, drag, and so on).
    func partMouseDown(_ v: NSView, _ e: NSEvent) -> Bool {
        if e.modifierFlags.contains(.control) { return false } // a secondary click: the context menu
        return selectClick(v, e) || selectableMouseDown(v, e)
    }

    /// Selects all of a Selectable's texts.
    func selectAllIn(_ s: AsukaNode) {
        let texts = selectableTexts(s)
        guard let last = texts.indices.last else { return }
        let st = selections[s.id] ?? AsukaSelectableState()
        selections[s.id] = st
        st.anchor = AsukaTextPlace(text: 0, char: 0)
        st.focus = AsukaTextPlace(text: last, char: length(texts[last]))
        st.granularity = .selectByCharacter
        showSelection(s)
    }

    /// The selected text, its texts' parts a paragraph each, without the
    /// inline components (nil: nothing is selected).
    func selectableText(_ s: AsukaNode) -> String? {
        guard selections[s.id] != nil else { return nil }
        let texts = selectableTexts(s)
        let ranges = selectionRanges(s)
        var parts: [String] = []
        for (i, t) in texts.enumerated() where i < ranges.count && ranges[i].length > 0 {
            guard let str = t.rich?.view.textStorage?.string as NSString? else { continue }
            parts.append(str.substring(with: ranges[i]).replacingOccurrences(of: "\u{FFFC}", with: ""))
        }
        return parts.isEmpty ? nil : parts.joined(separator: "\n")
    }

    /// ⌘C in a text of a Selectable: its selection copied. False: n isn't
    /// in one, or nothing is selected.
    func copySelectable(_ n: AsukaNode) -> Bool {
        guard let s = selectableOf(n), let text = selectableText(s) else { return false }
        let pb = NSPasteboard.general
        pb.clearContents()
        pb.setString(text, forType: .string)
        return true
    }

    func selectableReleased(_ n: AsukaNode) {
        if n.kind == .selectable { selections.removeValue(forKey: n.id) }
    }
}
