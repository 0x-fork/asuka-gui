// Asuka's own words (internal/l10n's catalogs: an alert's only button, a
// back button, a snackbar's close, the app menu's About and Quit, the
// updater's...), in the language the app shows (the Locale command's).
// STUB: English only, until WordsTable.swift is generated.
import Foundation

/// Asuka's word key (internal/l10n/messages' keys: "ok", "back", "about",
/// ...) in the language shown, its {placeholders} replaced by args.
@MainActor func asukaWord(_ key: String, _ args: [String: String] = [:]) -> String {
    var s = asukaEnglishWords[key] ?? key
    for (k, v) in args { s = s.replacingOccurrences(of: "{" + k + "}", with: v) }
    return s
}

/// A tab's or a sidebar row's badge for a screen reader: a count as so
/// many items ("3 items"), other text as it is.
@MainActor func asukaBadge(_ text: String) -> String {
    guard let n = Int(text.trimmingCharacters(in: .whitespaces)), n >= 0 else { return text }
    return n == 1 ? "1 item" : "\(n) items"
}

/// A title with its badge, if it has one, for a screen reader: "Inbox, 3
/// items".
@MainActor func asukaBadged(_ title: String, _ badge: String) -> String {
    badge.isEmpty ? title : title + ", " + asukaBadge(badge)
}

private let asukaEnglishWords: [String: String] = [
    "ok": "OK", "search": "Search", "back": "Back", "moreOptions": "More options", "decrease": "Decrease",
    "increase": "Increase", "about": "About {app}", "hide": "Hide {app}", "quit": "Quit {app}",
    "hideOthers": "Hide Others", "zoom": "Zoom", "cancel": "Cancel", "takePhoto": "Take Photo",
    "addToContacts": "Add to Contacts", "addToCalendar": "Add to Calendar", "newEvent": "New Event",
    "checkForUpdates": "Check for Updates…", "close": "Close",
]
