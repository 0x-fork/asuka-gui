// asuka/http on iOS and macOS: URLSession, the system's own client
// (proxies, trusted certificates, the shared cache and cookies).
import Foundation

final class URLSessionTransport: Transport {
    func send(method: String, url: String, headers: [String: String], body: Data, timeout: TimeInterval) async throws -> Response {
        guard let u = URL(string: url) else {
            throw URLError(.badURL)
        }
        var req = URLRequest(url: u)
        req.httpMethod = method
        if timeout > 0 {
            req.timeoutInterval = timeout
        }
        for (k, v) in headers {
            req.setValue(v, forHTTPHeaderField: k)
        }
        if !body.isEmpty {
            req.httpBody = body
        }
        let (data, resp) = try await URLSession.shared.data(for: req)
        let http = resp as? HTTPURLResponse
        var out: [String: String] = [:]
        for (k, v) in http?.allHeaderFields ?? [:] {
            out["\(k)".lowercased()] = "\(v)"
        }
        return Response(status: http?.statusCode ?? 0, url: resp.url?.absoluteString ?? url, headers: out, body: data)
    }
}

func makeTransport() throws -> any Transport { URLSessionTransport() }
