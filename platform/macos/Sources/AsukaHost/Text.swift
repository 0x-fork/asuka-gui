// STUB (part: text). Rich text (a non-editable NSTextView, inline
// components as attachments), Selectable, multiline and rich fields,
// selection, insertText, formatting and links, paste types, the field
// editor, and keys (the Keys prop, key events). Ports host/darwin's
// rich.go, richfield.go, textinput.go, keys.go and views.go's multiline
// field. The text agent replaces this file (and may split it), keeping
// these functions' signatures.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

@MainActor final class AsukaRich {}
@MainActor final class AsukaRichField {}

/// Watches the keyboard: views' Keys, the app's shortcuts.
@MainActor final class AsukaKeyWatcher {
    static let shared = AsukaKeyWatcher()
    func watch() {}
}

@MainActor extension AsukaRuntime {
    func makeRich(_ n: AsukaNode) -> NSView { AsukaView(frame: .zero) }
    func setRichProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func measureRich(_ n: AsukaNode, _ c: AsukaOffer) -> CGSize { .zero }
    /// Its text typeset again (a prop it's made of changed).
    func richInvalidate(_ n: AsukaNode) {}
    func richContentInset(_ n: AsukaNode) {}
    /// Places a rich text's inline components at their attachments.
    func placeInlines(_ n: AsukaNode) {}
    /// Makes a TextField a text view in a scroll view (n.textView).
    func makeMultiline(_ n: AsukaNode) { n.multiline = true }
    func measureMultiline(_ n: AsukaNode, _ c: AsukaOffer) -> CGSize { CGSize(width: c.finiteWidth ?? 200, height: 22) }
    func plainTextView(_ n: AsukaNode) {}
    func textViewPlaceholder(_ n: AsukaNode) {}
    func setSelection(_ n: AsukaNode, _ b: [UInt8]) {}
    func applyPendingSelection(_ n: AsukaNode) {}
    func setRichField(_ n: AsukaNode, _ b: [UInt8]) {}
    /// Types text into a field, as the keyboard would.
    func insertText(_ n: AsukaNode, _ s: String) {
        guard let f = n.view as? NSTextField else { return }
        f.stringValue += s
        n.text = f.stringValue
        let t = n.text
        emit(n, .textChanged) { AsukaEvents.textChanged($0, t) }
    }
    func formatField(_ n: AsukaNode, _ b: [UInt8]) {}
    func setFieldLink(_ n: AsukaNode, _ url: String) {}
    /// A command a one-line field's editor runs (not Return): whether it
    /// took it.
    func textCommand(_ n: AsukaNode, _ cmd: Selector) -> Bool { false }
    /// n's Keys prop changed.
    func keysChanged(_ n: AsukaNode) {}

    // Hooks (Parts.swift).
    func textDestroyed(_ n: AsukaNode) {}
    func textReleased(_ n: AsukaNode) {}
    func textPlaced(_ n: AsukaNode) {}
    func textColors(_ n: AsukaNode) {}
    func textDirection(_ n: AsukaNode, _ changed: Bool) {}
    func textJSON(_ n: AsukaNode, _ d: inout [String: Any]) {}
    func registerTextModules() {}
}
