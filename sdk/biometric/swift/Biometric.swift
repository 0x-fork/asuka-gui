// platform/biometric on iOS and the Mac: LocalAuthentication (Face ID, Touch
// ID, Optic ID; the device's passcode, or the Mac's password, as the
// fallback).
import Foundation
import LocalAuthentication

final class LocalAuthenticator: Authenticator {
    func check() async throws -> Status {
        let c = LAContext()
        let enrolled = c.canEvaluatePolicy(.deviceOwnerAuthenticationWithBiometrics, error: nil)
        var kind = Kind.none
        switch c.biometryType {
        case .touchID: kind = .fingerprint
        case .faceID: kind = .face
        default:
            if #available(iOS 17.0, macOS 14.0, *), c.biometryType == .opticID { kind = .optic }
        }
        let passcode = LAContext().canEvaluatePolicy(.deviceOwnerAuthentication, error: nil)
        return Status(kind: kind, enrolled: enrolled, passcode: passcode)
    }

    func authenticate(reason: String, cancel: String, noPasscode: Bool) async throws {
        let c = LAContext()
        if !cancel.isEmpty { c.localizedCancelTitle = cancel }
        if noPasscode { c.localizedFallbackTitle = "" }
        let policy: LAPolicy = noPasscode ? .deviceOwnerAuthenticationWithBiometrics : .deviceOwnerAuthentication
        guard c.canEvaluatePolicy(policy, error: nil) else { throw BiometricError.notAvailable }
        do {
            _ = try await c.evaluatePolicy(policy, localizedReason: reason.isEmpty ? " " : reason)
        } catch let e as LAError {
            switch e.code {
            case .userCancel, .appCancel, .systemCancel, .userFallback: throw BiometricError.canceled
            case .biometryNotAvailable, .biometryNotEnrolled, .biometryLockout, .passcodeNotSet: throw BiometricError.notAvailable
            case .authenticationFailed: throw BiometricError.failed
            default: throw e
            }
        }
    }
}

func makeAuthenticator() throws -> any Authenticator { LocalAuthenticator() }
