// asuka/host on iOS and macOS: the host's own modules (AsukaHostModules),
// on the main thread.
import Foundation
import Asuka

final class HostModules: Modules {
    @MainActor
    func call(module: String, method: String, args: Data) async throws -> Data {
        try await withCheckedThrowingContinuation { (k: CheckedContinuation<Data, Error>) in
            AsukaHostModules.call(module, method, args) { k.resume(with: $0) }
        }
    }

    @MainActor func send(module: String, method: String, args: Data) throws {
        AsukaHostModules.call(module, method, args) { _ in }
    }
}

func makeModules() throws -> any Modules { HostModules() }
