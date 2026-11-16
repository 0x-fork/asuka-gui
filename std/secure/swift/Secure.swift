// asuka/secure on iOS and the Mac: the Keychain, a generic password per
// key under the app's ID (its share extension's too: AsukaAppID), kept
// on this device only, readable once it's been unlocked after a restart.
import Foundation
import Security

final class SecKeychain: Keychain {
    let service = Bundle.main.object(forInfoDictionaryKey: "AsukaAppID") as? String
        ?? Bundle.main.bundleIdentifier ?? ProcessInfo.processInfo.processName

    /// A key's item. On the Mac it's in the login keychain, which an app
    /// signed ad hoc can use too (asuka dev's).
    func query(_ key: String) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service, kSecAttrAccount as String: key]
    }

    func get(key: String) async throws -> String? {
        var q = query(key)
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: CFTypeRef?
        let st = SecItemCopyMatching(q as CFDictionary, &out)
        if st == errSecItemNotFound { return nil }
        guard st == errSecSuccess, let d = out as? Data else { throw Self.error(st) }
        return String(decoding: d, as: UTF8.self)
    }

    func set(key: String, value: String) async throws {
        let q = query(key)
        let st = SecItemUpdate(q as CFDictionary, [kSecValueData as String: Data(value.utf8)] as CFDictionary)
        if st == errSecSuccess { return }
        guard st == errSecItemNotFound else { throw Self.error(st) }
        var add = q
        add[kSecValueData as String] = Data(value.utf8)
        add[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        let st2 = SecItemAdd(add as CFDictionary, nil)
        guard st2 == errSecSuccess else { throw Self.error(st2) }
    }

    func remove(key: String) async throws {
        let st = SecItemDelete(query(key) as CFDictionary)
        guard st == errSecSuccess || st == errSecItemNotFound else { throw Self.error(st) }
    }

    static func error(_ st: OSStatus) -> Error {
        let msg = SecCopyErrorMessageString(st, nil) as String? ?? "status \(st)"
        return NSError(domain: NSOSStatusErrorDomain, code: Int(st), userInfo: [NSLocalizedDescriptionKey: "the keychain: \(msg)"])
    }
}

func makeKeychain() throws -> any Keychain { SecKeychain() }
