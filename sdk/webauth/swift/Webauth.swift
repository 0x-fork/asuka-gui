// platform/webauth on iOS and the Mac: ASWebAuthenticationSession, its sheet
// on the app's window. Cancelling the task cancels the session.
import AuthenticationServices
import Foundation
import Asuka

final class AuthSheet: Sheet {
    func open(url: String, callbackScheme: String, ephemeral: Bool) async throws -> String {
        guard let u = URL(string: url), u.scheme == "https" || u.scheme == "http" else {
            throw NSError(domain: "webauth", code: 0, userInfo: [NSLocalizedDescriptionKey: "webauth: not a web URL: \(url)"])
        }
        let run = await MainActor.run { AuthRun() }
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (c: CheckedContinuation<String, Error>) in
                Task { @MainActor in run.start(u, scheme: callbackScheme, ephemeral: ephemeral, c) }
            }
        } onCancel: {
            Task { @MainActor in run.cancel() }
        }
    }
}

/// One sign-in: its session, its window, its answer (once).
@MainActor final class AuthRun: NSObject, ASWebAuthenticationPresentationContextProviding {
    private static var running = Set<AuthRun>()
    private var session: ASWebAuthenticationSession?
    private var done: CheckedContinuation<String, Error>?
    private var canceled = false
    private var window: ASPresentationAnchor?

    func start(_ url: URL, scheme: String, ephemeral: Bool, _ c: CheckedContinuation<String, Error>) {
        done = c
        if canceled { return finish(.failure(CancellationError())) }
        guard let w = AsukaNative.window else {
            return finish(.failure(AsukaNativeError.unsupported("webauth: no window to sign in over")))
        }
        window = w
        Self.running.insert(self)
        let s = ASWebAuthenticationSession(url: url, callbackURLScheme: scheme.isEmpty ? nil : scheme) { [weak self] callback, error in
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    if let callback { self?.finish(.success(callback.absoluteString)); return }
                    if let e = error as? ASWebAuthenticationSessionError, e.code == .canceledLogin {
                        self?.finish(.failure(WebAuthError.canceled)); return
                    }
                    self?.finish(.failure(error ?? WebAuthError.canceled))
                }
            }
        }
        s.presentationContextProvider = self
        s.prefersEphemeralWebBrowserSession = ephemeral
        session = s
        if !s.start() { finish(.failure(NSError(domain: "webauth", code: 0, userInfo: [NSLocalizedDescriptionKey: "webauth: couldn't start the sign-in"]))) }
    }

    func cancel() {
        canceled = true
        session?.cancel()
        finish(.failure(CancellationError()))
    }

    private func finish(_ r: Result<String, Error>) {
        guard let d = done else { return }
        done = nil
        session = nil
        Self.running.remove(self)
        d.resume(with: r)
    }

    nonisolated func presentationAnchor(for session: ASWebAuthenticationSession) -> ASPresentationAnchor {
        MainActor.assumeIsolated { window ?? ASPresentationAnchor() }
    }
}

func makeSheet() throws -> any Sheet { AuthSheet() }
