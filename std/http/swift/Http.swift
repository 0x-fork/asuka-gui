// asuka/http on iOS and macOS: URLSession, the system's own client
// (proxies, trusted certificates, the shared cache and cookies).
import Foundation
import Network

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

extension URLSessionTransport {
    func download(method: String, url: String, headers: [String: String], body: Data, timeout: TimeInterval, path: String, progress: @escaping (Int, Int) -> Void) async throws -> Response {
        guard let u = URL(string: url) else {
            throw URLError(.badURL)
        }
        var req = URLRequest(url: u)
        req.httpMethod = method
        if timeout > 0 {
            req.timeoutInterval = timeout  // the longest wait for more
        }
        for (k, v) in headers {
            req.setValue(v, forHTTPHeaderField: k)
        }
        if !body.isEmpty {
            req.httpBody = body
        }
        let d = Download(path: path, progress: progress)
        // A session of its own, for its delegate: the shared cookies and
        // cache (the default configuration's), as URLSession.shared has.
        let session = URLSession(configuration: .default, delegate: d, delegateQueue: nil)
        defer { session.finishTasksAndInvalidate() }
        let task = session.downloadTask(with: req)
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (k: CheckedContinuation<Response, Error>) in
                d.lock.lock()
                d.continuation = k
                d.lock.unlock()
                task.resume()
            }
        } onCancel: {
            task.cancel()
        }
    }
}

/// A download's delegate: progress as it comes (a few times a second),
/// the file moved to its path once it's all in, the answer.
private final class Download: NSObject, URLSessionDownloadDelegate {
    let path: String
    let progress: (Int, Int) -> Void
    let lock = NSLock()
    var continuation: CheckedContinuation<Response, Error>?
    var answer: Result<Response, Error>?
    var reported = Date.distantPast

    init(path: String, progress: @escaping (Int, Int) -> Void) {
        self.path = path
        self.progress = progress
    }

    func urlSession(_ s: URLSession, downloadTask: URLSessionDownloadTask, didWriteData bytes: Int64, totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
        let now = Date()
        if now.timeIntervalSince(reported) < 0.1 { return }
        reported = now
        progress(Int(totalBytesWritten), totalBytesExpectedToWrite == NSURLSessionTransferSizeUnknown ? -1 : Int(totalBytesExpectedToWrite))
    }

    func urlSession(_ s: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        // The file goes before this returns: move it now.
        let to = URL(fileURLWithPath: path)
        do {
            let fm = FileManager.default
            try fm.createDirectory(at: to.deletingLastPathComponent(), withIntermediateDirectories: true)
            if fm.fileExists(atPath: to.path) {
                _ = try fm.replaceItemAt(to, withItemAt: location)
            } else {
                try fm.moveItem(at: location, to: to)
            }
            let size = (try? fm.attributesOfItem(atPath: to.path)[.size] as? NSNumber)?.intValue ?? 0
            progress(size, size)
            let http = downloadTask.response as? HTTPURLResponse
            var out: [String: String] = [:]
            for (k, v) in http?.allHeaderFields ?? [:] {
                out["\(k)".lowercased()] = "\(v)"
            }
            answer = .success(Response(status: http?.statusCode ?? 0, url: downloadTask.response?.url?.absoluteString ?? to.absoluteString, headers: out, body: Data()))
        } catch {
            answer = .failure(error)
        }
    }

    func urlSession(_ s: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        lock.lock()
        let k = continuation
        continuation = nil
        lock.unlock()
        if let e = error {
            k?.resume(throwing: e)
        } else {
            k?.resume(with: answer ?? .failure(URLError(.cannotWriteToFile)))
        }
    }
}

func makeTransport() throws -> any Transport { URLSessionTransport() }

// ---- LocalServer: Network.framework's listener on the loopback ----

// Its state is the queue's: everything that reads or writes it runs there.
final class LoopbackServers: LocalServer, @unchecked Sendable {
    private let queue = DispatchQueue(label: "asuka.http.loopback")
    private var servers: [Int: Server] = [:]

    private final class Server: @unchecked Sendable {
        let listener: NWListener
        let path: String
        let page: String
        var started = false  // listen has answered
        var arrived: [String] = []
        var waiting: [(id: UUID, k: CheckedContinuation<String, Error>)] = []
        init(listener: NWListener, path: String, page: String) {
            self.listener = listener
            self.path = path
            self.page = page
        }
    }

    func listen(path: String, page: String) async throws -> Int {
        let params = NWParameters.tcp
        params.requiredLocalEndpoint = .hostPort(host: .ipv4(.loopback), port: .any)
        let listener = try NWListener(using: params)
        let server = Server(listener: listener, path: path, page: page)
        listener.newConnectionHandler = { [weak self] c in self?.serve(c, server) }
        return try await withCheckedThrowingContinuation { (k: CheckedContinuation<Int, Error>) in
            listener.stateUpdateHandler = { [weak self] state in
                guard !server.started else { return }
                switch state {
                case .ready:
                    server.started = true
                    let port = Int(listener.port?.rawValue ?? 0)
                    self?.servers[port] = server
                    k.resume(returning: port)
                case .failed(let e):
                    server.started = true
                    k.resume(throwing: e)
                case .cancelled:
                    server.started = true
                    k.resume(throwing: CancellationError())
                default:
                    break
                }
            }
            listener.start(queue: queue)
        }
    }

    // serve reads a request's head and answers it: the page for the
    // server's path (its URL to a waiting next), else a 404.
    private func serve(_ c: NWConnection, _ server: Server) {
        c.start(queue: queue)
        var head = Data()
        func read() {
            c.receive(minimumIncompleteLength: 1, maximumLength: 8192) { [weak self] data, _, done, error in
                if let d = data { head.append(d) }
                let text = String(decoding: head, as: UTF8.self)
                if text.contains("\r\n\r\n") || done || error != nil || head.count >= 8192 {
                    self?.answer(c, server, text)
                } else {
                    read()
                }
            }
        }
        read()
    }

    private func answer(_ c: NWConnection, _ server: Server, _ head: String) {
        let parts = head.split(separator: "\r\n", maxSplits: 1).first?.split(separator: " ") ?? []
        let target = parts.count >= 2 ? String(parts[1]) : ""
        let ours = target == server.path || target.hasPrefix(server.path + "?")
        let body = ours ? Data(server.page.utf8) : Data()
        let status = ours ? "200 OK" : "404 Not Found"
        var out = Data("HTTP/1.1 \(status)\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: \(body.count)\r\nConnection: close\r\n\r\n".utf8)
        out.append(body)
        c.send(content: out, completion: .contentProcessed { _ in c.cancel() })
        guard ours else { return }
        let url = "http://127.0.0.1:\(server.listener.port?.rawValue ?? 0)\(target)"
        if !server.waiting.isEmpty {
            server.waiting.removeFirst().k.resume(returning: url)
        } else {
            server.arrived.append(url)
        }
    }

    func next(port: Int) async throws -> String {
        let id = UUID()
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (k: CheckedContinuation<String, Error>) in
                queue.async {
                    guard let s = self.servers[port] else {
                        k.resume(throwing: URLError(.cannotConnectToHost))
                        return
                    }
                    if !s.arrived.isEmpty {
                        k.resume(returning: s.arrived.removeFirst())
                    } else {
                        s.waiting.append((id, k))
                    }
                }
            }
        } onCancel: {
            queue.async {
                guard let s = self.servers[port], let i = s.waiting.firstIndex(where: { $0.id == id }) else { return }
                s.waiting.remove(at: i).k.resume(throwing: CancellationError())
            }
        }
    }

    func close(port: Int) throws {
        queue.async {
            guard let s = self.servers.removeValue(forKey: port) else { return }
            s.listener.cancel()
            for w in s.waiting { w.k.resume(throwing: CancellationError()) }
            s.waiting = []
        }
    }
}

private let loopbackServers = LoopbackServers()

func makeLocalServer() throws -> any LocalServer { loopbackServers }
