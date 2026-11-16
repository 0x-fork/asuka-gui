// The web views' module, "web": it runs script in a WebView for its value
// (a Ref's eval: the core's ui.web_eval_result) and reads the web views'
// cookies (webCookies: ui.web_cookies). Ports host/darwin's webmore.go.
// Its "js" module isn't: asuka/js is a native package with its own Swift,
// and nothing calls the host's.
import AppKit
import WebKit
#if canImport(AsukaOps)
import AsukaOps
#endif

/// Runs s in the page's world and answers with its value as JSON: the last
/// statement's, or what its promise resolves to. A page whose Content
/// Security Policy blocks eval answers evalBlocked instead, before any of s
/// has run.
let asukaEvalBody = """
    try { (0, eval)("0") } catch { return "!eval" }
    const v = await (0, eval)(s); return JSON.stringify(v === undefined ? null : v)
    """

/// asukaEvalBody's answer on a page that blocks eval: not JSON, so no value
/// of a script's is mistaken for it.
let asukaEvalBlocked = "!eval"

@MainActor extension AsukaRuntime {
    func registerWebModules() {
        registerModule("web", [
            // args: u32 a WebView's node id, then the script; answers with the
            // script's value as JSON.
            "eval": { args, reply in
                MainActor.assumeIsolated {
                    let a = [UInt8](args)
                    guard a.count >= 4 else {
                        reply(.failure(AsukaModuleError("bad arguments")))
                        return
                    }
                    let rt = AsukaRuntime.shared
                    guard let w = rt.nodes[a.u32(0)]?.web else {
                        reply(.failure(AsukaModuleError("not a WebView")))
                        return
                    }
                    rt.evalIn(w.view, String(decoding: a[4...], as: UTF8.self), reply)
                }
            },
            // args: a URL; answers with the cookies the WebViews have for it,
            // as a Cookie header.
            "cookies": { args, reply in
                MainActor.assumeIsolated {
                    guard let u = URL(string: String(decoding: args, as: UTF8.self)), let host = u.host, !host.isEmpty else {
                        reply(.success(Data()))
                        return
                    }
                    WKWebsiteDataStore.default().httpCookieStore.getAllCookies { cookies in
                        reply(.success(Data(asukaCookieHeader(u, cookies).utf8)))
                    }
                }
            },
        ])
    }

    /// Runs script in a web view (callAsyncJavaScript) and answers with its
    /// value as JSON, or what it threw. On a page that blocks eval, it runs
    /// as EvalJS runs it (evaluateJavaScript, which no Content Security
    /// Policy applies to): its last statement's value.
    func evalIn(_ v: WKWebView, _ script: String, _ reply: @escaping (Result<Data, Error>) -> Void) {
        callAsync(v, asukaEvalBody, ["s": script]) { [weak self] r in
            switch r {
            case .failure(let e): reply(.failure(e))
            case .success(asukaEvalBlocked?): self?.evalNative(v, script, reply)
            case .success(let s?): reply(.success(Data(s.utf8)))
            case .success(nil): reply(.success(Data("null".utf8)))
            }
        }
    }

    /// Runs script as the program it is and answers with its last
    /// statement's value as JSON, which the page's JSON.stringify writes:
    /// the value WebKit returns (strings, numbers, dates, arrays and objects
    /// of them) goes back in as an argument.
    func evalNative(_ v: WKWebView, _ script: String, _ reply: @escaping (Result<Data, Error>) -> Void) {
        v.evaluateJavaScript(script) { [weak self] result, err in
            MainActor.assumeIsolated {
                if let err {
                    reply(.failure(asukaJSError(err)))
                    return
                }
                guard let result, !(result is NSNull) else { // undefined or null
                    reply(.success(Data("null".utf8)))
                    return
                }
                self?.callAsync(v, "return JSON.stringify(v)", ["v": result]) { r in
                    reply(r.map { Data(($0 ?? "null").utf8) })
                }
            }
        }
    }

    /// Runs body as an async function in the page's world, with args, and
    /// answers on the main thread with the string it returned (nil for
    /// anything else) or what it threw.
    func callAsync(_ v: WKWebView, _ body: String, _ args: [String: Any], _ answer: @escaping (Result<String?, Error>) -> Void) {
        // The Objective-C method, not the Swift overlay's, which traps when
        // a function returns nothing (neither a result nor an error).
        v.__callAsyncJavaScript(body, arguments: args, inFrame: nil, in: .page) { result, err in
            let r: Result<String?, Error> = err.map { .failure(asukaJSError($0)) } ?? .success(result as? String)
            DispatchQueue.main.async { answer(r) }
        }
    }
}

/// A WKWebView's error running script: the message of what it threw.
func asukaJSError(_ e: Error) -> Error {
    let n = e as NSError
    if n.domain == WKErrorDomain && n.code == 5 { // WKErrorJavaScriptResultTypeIsUnsupported
        return AsukaModuleError("the script's value is a promise, a function or another value WebKit can't return: the page's Content Security Policy blocks eval, so the script ran as EvalJS runs it, and a promise isn't awaited")
    }
    let m = n.userInfo["WKJavaScriptExceptionMessage"] as? String ?? ""
    return AsukaModuleError(m.isEmpty ? e.localizedDescription : m)
}

/// The cookies that go with a request to u, as a Cookie header.
func asukaCookieHeader(_ u: URL, _ cookies: [HTTPCookie]) -> String {
    let host = (u.host ?? "").lowercased()
    var path = u.path
    if path.isEmpty { path = "/" }
    let now = Date()
    return cookies.filter { c in
        let d = c.domain.lowercased()
        let domainOK = d.hasPrefix(".") ? host == String(d.dropFirst()) || host.hasSuffix(d) : host == d
        return domainOK && path.hasPrefix(c.path) && (!c.isSecure || u.scheme == "https") && (c.expiresDate.map { $0 > now } ?? true)
    }.map { "\($0.name)=\($0.value)" }.joined(separator: "; ")
}
