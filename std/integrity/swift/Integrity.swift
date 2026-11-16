// asuka/integrity on iOS and the Mac: App Attest (DCAppAttestService),
// its client data hash the SHA-256 of what the server gave. Play
// Integrity is Android's.
import CryptoKit
import DeviceCheck
import Foundation
import Asuka

final class AppAttest: Attest {
    private var service: DCAppAttestService? {
        if #available(macOS 11.0, *) { return DCAppAttestService.shared }
        return nil
    }

    func supported() throws -> Bool { service?.isSupported ?? false }

    func generateKey() async throws -> String {
        guard let s = service, s.isSupported else { throw AsukaNativeError.unsupported("integrity: App Attest isn't on this device") }
        return try await s.generateKey()
    }

    func attestKey(keyId: String, challenge: Data) async throws -> Data {
        guard let s = service else { throw AsukaNativeError.unsupported("integrity: App Attest isn't on this device") }
        return try await s.attestKey(keyId, clientDataHash: Data(SHA256.hash(data: challenge)))
    }

    func assertion(keyId: String, data: Data) async throws -> Data {
        guard let s = service else { throw AsukaNativeError.unsupported("integrity: App Attest isn't on this device") }
        return try await s.generateAssertion(keyId, clientDataHash: Data(SHA256.hash(data: data)))
    }

    func prepare(cloudProjectNumber: Int) async throws {
        throw AsukaNativeError.unsupported("integrity: Play Integrity is Android's: use App Attest here")
    }

    func token(requestHash: String) async throws -> String {
        throw AsukaNativeError.unsupported("integrity: Play Integrity is Android's: use App Attest here")
    }
}

func makeAttest() throws -> any Attest { AppAttest() }
