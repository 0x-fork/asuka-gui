// platform/tracking on iOS and the Mac: App Tracking Transparency.
import AppTrackingTransparency
import Foundation

final class TransparencyTracker: Tracker {
    func current() throws -> Status { Self.status(ATTrackingManager.trackingAuthorizationStatus) }

    func request() async throws -> Status {
        Self.status(await ATTrackingManager.requestTrackingAuthorization())
    }

    static func status(_ s: ATTrackingManager.AuthorizationStatus) -> Status {
        switch s {
        case .restricted: return .restricted
        case .denied: return .denied
        case .authorized: return .allowed
        default: return .notAsked
        }
    }
}

func makeTracker() throws -> any Tracker { TransparencyTracker() }
