// asuka/store on iOS and macOS: UserDefaults.
import Foundation

final class DefaultsPrefs: Prefs {
    private let defaults = DefaultsPrefs.suite()
    private func k(_ key: String) -> String { "asuka.store." + key }
    func get(key: String) throws -> String? { defaults.string(forKey: k(key)) }
    func set(key: String, value: String) throws { defaults.set(value, forKey: k(key)) }
    func remove(key: String) throws { defaults.removeObject(forKey: k(key)) }
    func flush() throws { defaults.synchronize() }
    func dir() throws -> String {
        // UserDefaults keeps the app's domain in Library/Preferences (an
        // App Group's in its container's).
        let fm = FileManager.default
        let lib = DefaultsPrefs.group().flatMap { fm.containerURL(forSecurityApplicationGroupIdentifier: $0)?.appendingPathComponent("Library") }
            ?? fm.urls(for: .libraryDirectory, in: .userDomainMask).first
        return lib?.appendingPathComponent("Preferences").path ?? ""
    }

    /// The App Group of an iOS app that runs in its extensions too (its
    /// share sheet, a push's rewrite: Info.plist's AsukaShareSheet,
    /// AsukaNotificationService), whose values they see; nil otherwise.
    static func group() -> String? {
        let info = Bundle.main.infoDictionary ?? [:]
        guard info["AsukaShareSheet"] as? Bool == true || info["AsukaNotificationService"] as? Bool == true else { return nil }
        return info["AsukaAppGroup"] as? String
    }

    /// The App Group's suite, with the values the app kept in its own
    /// moved there the first time it runs with extensions; the app's own
    /// without.
    static func suite() -> UserDefaults {
        guard let g = group(), let shared = UserDefaults(suiteName: g) else { return .standard }
        let moved = "asuka.storeMoved"
        if !Bundle.main.bundlePath.hasSuffix(".appex"), !shared.bool(forKey: moved) {
            let own = UserDefaults.standard
            for (k, v) in own.dictionaryRepresentation() where k.hasPrefix("asuka.store.") {
                if shared.object(forKey: k) == nil { shared.set(v, forKey: k) }
                own.removeObject(forKey: k)
            }
            shared.set(true, forKey: moved)
        }
        return shared
    }
}

func makePrefs() throws -> any Prefs { DefaultsPrefs() }
