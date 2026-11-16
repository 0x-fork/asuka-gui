// platform/js on iOS and the Mac: a WKWebView that's never shown, made for
// each script and let go after; callAsyncJavaScript awaits a promise.
import Foundation
import WebKit

final class WebEngine: Engine {
    func run(script: String, timeout: TimeInterval) async throws -> String {
        let page = await MainActor.run { ScriptPage(script, timeout: timeout) }
        return try await withTaskCancellationHandler {
            try await page.result()
        } onCancel: {
            Task { @MainActor in page.finish(.failure(CancellationError())) }
        }
    }
}

/// A script's page: a blank one, where the script runs once it's loaded.
@MainActor final class ScriptPage: NSObject, WKNavigationDelegate {
    private static var running = Set<ScriptPage>()
    private let web = WKWebView(frame: .zero, configuration: WKWebViewConfiguration())
    private let script: String
    private var answer: Result<String, Error>?
    private var waiting: CheckedContinuation<String, Error>?

    init(_ script: String, timeout: TimeInterval) {
        self.script = script
        super.init()
        Self.running.insert(self)
        web.navigationDelegate = self
        web.loadHTMLString("<!doctype html><html></html>", baseURL: nil)
        DispatchQueue.main.asyncAfter(deadline: .now() + max(timeout, 0)) { [weak self] in
            self?.finish(.failure(JsError.timeout))
        }
    }

    nonisolated func result() async throws -> String {
        try await withCheckedThrowingContinuation { c in
            Task { @MainActor in
                if let a = self.answer { c.resume(with: a) } else { self.waiting = c }
            }
        }
    }

    func webView(_ w: WKWebView, didFinish navigation: WKNavigation!) {
        // Indirect eval: the global scope, and the last statement's value.
        let body = "const v = await (0, eval)(s); return JSON.stringify(v === undefined ? null : v)"
        w.callAsyncJavaScript(body, arguments: ["s": script], in: nil, in: .page) { [weak self] r in
            switch r {
            case .success(let v): self?.finish(.success((v as? String) ?? "null"))
            case .failure(let e):
                let m = (e as NSError).userInfo["WKJavaScriptExceptionMessage"] as? String ?? e.localizedDescription
                self?.finish(.failure(NSError(domain: "js", code: 0, userInfo: [NSLocalizedDescriptionKey: m])))
            }
        }
    }

    func webView(_ w: WKWebView, didFail navigation: WKNavigation!, withError error: Error) { finish(.failure(error)) }

    func finish(_ r: Result<String, Error>) {
        guard answer == nil else { return }
        answer = r
        waiting?.resume(with: r)
        waiting = nil
        web.stopLoading()
        web.navigationDelegate = nil
        Self.running.remove(self)
    }
}

func makeEngine() throws -> any Engine { WebEngine() }
