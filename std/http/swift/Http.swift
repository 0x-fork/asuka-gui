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
