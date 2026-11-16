// Asuka's own words (internal/l10n's catalogs: an alert's only button, a
// back button, a snackbar's close, the app menu's About and Quit, the
// updater's...), in the language the app shows (the Locale command's):
// the desktop hosts' shared C (platform/desktop's words.c, its tables
// generated from the catalogs), as Windows and Linux say them.
import Foundation
@_implementationOnly import CAsuka

/// Asuka's word key (internal/l10n/messages' keys: "ok", "back", "about",
/// ...) in the language shown, its {placeholders} (and a {count, plural,
/// ...}) filled from args.
@MainActor func asukaWord(_ key: String, _ args: [String: String] = [:]) -> String {
    let pairs = args.sorted { $0.key < $1.key }.flatMap { [$0.key, $0.value] }
    let cs = pairs.map { strdup($0) }
    defer { cs.forEach { free($0) } }
    var vars: [UnsafePointer<CChar>?] = cs.map { UnsafePointer($0) }
    var out = [CChar](repeating: 0, count: 1024)
    let locale = AsukaRuntime.shared.locale
    vars.withUnsafeMutableBufferPointer { v in
        _ = dsk_word_format(&out, out.count, locale, key, v.baseAddress, cs.count)
    }
    return String(cString: out)
}

/// A tab's or a sidebar row's badge for a screen reader: a count as so
/// many items ("3 items"), other text as it is.
@MainActor func asukaBadge(_ text: String) -> String {
    guard let n = Int(text.trimmingCharacters(in: .whitespaces)), n >= 0 else { return text }
    return asukaWord("badgeCount", ["count": String(n)])
}

/// A title with its badge, if it has one, for a screen reader: "Inbox, 3
/// items".
@MainActor func asukaBadged(_ title: String, _ badge: String) -> String {
    badge.isEmpty ? title : title + ", " + asukaBadge(badge)
}
