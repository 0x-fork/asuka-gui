// platform/review on iOS and the Mac: StoreKit's rating prompt.
import Foundation
import StoreKit
import Asuka
#if canImport(UIKit)
import UIKit
#else
import AppKit
#endif

final class StoreReview: Store {
    func request() async throws {
        #if canImport(UIKit)
        try await MainActor.run {
            guard let scene = AsukaNative.window?.windowScene else {
                throw AsukaNativeError.unsupported("review: no screen to ask on")
            }
            AppStore.requestReview(in: scene)
        }
        #else
        await MainActor.run {
            if #available(macOS 13, *), let vc = AsukaNative.window?.contentViewController {
                AppStore.requestReview(in: vc)
            } else {
                SKStoreReviewController.requestReview()
            }
        }
        #endif
    }
}

func makeStore() throws -> any Store { StoreReview() }
