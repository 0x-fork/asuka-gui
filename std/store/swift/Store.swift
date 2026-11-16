// asuka/store on iOS and macOS: UserDefaults.
import Foundation

final class DefaultsPrefs: Prefs {
    private let defaults = UserDefaults.standard
    private func k(_ key: String) -> String { "asuka.store." + key }
    func get(key: String) throws -> String? { defaults.string(forKey: k(key)) }
    func set(key: String, value: String) throws { defaults.set(value, forKey: k(key)) }
    func remove(key: String) throws { defaults.removeObject(forKey: k(key)) }
    func flush() throws { defaults.synchronize() }
    func dir() throws -> String {
        // UserDefaults keeps the app's domain in Library/Preferences.
        let lib = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask).first
        return lib?.appendingPathComponent("Preferences").path ?? ""
    }
}

func makePrefs() throws -> any Prefs { DefaultsPrefs() }
