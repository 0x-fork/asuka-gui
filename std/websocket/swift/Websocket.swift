// std/websocket on iOS and macOS: URLSessionWebSocketTask, the system's
// own client (proxies, the certificates the user trusts, cookies).
//
// The connections share one session, whose delegate hands each task's
// events to its connection. A connection asks its task for one message at
// a time, when it's read: what comes while no one waits (a read
// cancelled) is kept for the next. Its end is the close the server sent
// (or the one it answered), else 1006: the connection ended without one.
import Foundation
import Asuka

func makeClient() throws -> any Client { URLSessionClient() }

func makeSocket() throws -> any Socket {
    throw AsukaNativeError.unsupported("a socket comes from Client.connect")
}

final class URLSessionClient: Client {
    func connect(url: String, headers: [String: String], protocols: [String], timeout: TimeInterval) async throws -> any Socket {
        guard let u = URL(string: url), let scheme = u.scheme?.lowercased(), ["ws", "wss", "http", "https"].contains(scheme) else {
            throw URLError(.badURL)
        }
        var req = URLRequest(url: u)
        // The opening handshake's longest wait (the timer below says it
        // for the whole of it); an open connection waits as long as it
        // must for the next message.
        req.timeoutInterval = timeout > 0 ? timeout : Sockets.forever
        for (k, v) in headers {
            req.setValue(v, forHTTPHeaderField: k)
        }
        if !protocols.isEmpty {
            req.setValue(protocols.joined(separator: ", "), forHTTPHeaderField: "Sec-WebSocket-Protocol")
        }
        let task = Sockets.shared.session.webSocketTask(with: req)
        // As a browser's: no limit of URLSession's (1 MiB) on a message.
        task.maximumMessageSize = Int.max
        let c = Connection(task)
        Sockets.shared.add(task, c)
        return try await withTaskCancellationHandler {
            let p = try await withCheckedThrowingContinuation { (k: CheckedContinuation<String, Error>) in
                c.waitOpen(k, timeout: timeout)
                if Task.isCancelled { c.fail(URLError(.cancelled)) }  // before the handler was in
            }
            return URLSessionSocket(c, protocol: p)
        } onCancel: {
            c.fail(URLError(.cancelled))
        }
    }
}

/// The one session the connections share, and its delegate.
private final class Sockets: NSObject, URLSessionWebSocketDelegate, @unchecked Sendable {
    static let shared = Sockets()
    static let forever: TimeInterval = 1_000_000_000

    private let lock = NSLock()
    private var connections: [Int: Connection] = [:]
    private(set) var session: URLSession!

    override init() {
        super.init()
        let config = URLSessionConfiguration.default
        config.timeoutIntervalForResource = Sockets.forever
        session = URLSession(configuration: config, delegate: self, delegateQueue: nil)
    }

    func add(_ task: URLSessionTask, _ c: Connection) {
        lock.lock()
        connections[task.taskIdentifier] = c
        lock.unlock()
    }

    private func connection(_ task: URLSessionTask, remove: Bool = false) -> Connection? {
        lock.lock()
        defer { lock.unlock() }
        return remove ? connections.removeValue(forKey: task.taskIdentifier) : connections[task.taskIdentifier]
    }

    func urlSession(_ s: URLSession, webSocketTask: URLSessionWebSocketTask, didOpenWithProtocol p: String?) {
        connection(webSocketTask)?.opened(p ?? "")
    }

    func urlSession(_ s: URLSession, webSocketTask: URLSessionWebSocketTask, didCloseWith code: URLSessionWebSocketTask.CloseCode, reason: Data?) {
        connection(webSocketTask)?.closed(code: code.rawValue, reason: reason)
    }

    func urlSession(_ s: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        connection(task, remove: true)?.completed(error)
    }
}

/// A connection's state, under its lock: who waits for it to open, the
/// one message asked of the task (and who waits for it, or what came while
/// no one did), and how it ended.
private final class Connection: @unchecked Sendable {
    let task: URLSessionWebSocketTask
    private let lock = NSLock()
    private var opener: CheckedContinuation<String, Error>?
    private var timer: DispatchWorkItem?
    private var isOpen = false
    /// A message asked of the task, not yet in.
    private var asking = false
    /// What came while no one waited.
    private var kept: Result<Frame, Error>?
    private var reader: (id: UInt64, k: CheckedContinuation<Frame, Error>)?
    private var readers: UInt64 = 0
    /// The close: the server's (or the one it answered), else the one
    /// shut down with; the frame every read gives once the task's done.
    private var close: Frame?
    private var done = false
    /// A failure not yet reported: the next read throws it (once).
    private var failure: Error?
    /// The app let it go, or shut it down: its end isn't a failure.
    private var ending = false
    /// The close the app shut it down with: its end when the server's
    /// answer doesn't say.
    private var sent: Frame?

    init(_ task: URLSessionWebSocketTask) {
        self.task = task
    }

    func waitOpen(_ k: CheckedContinuation<String, Error>, timeout: TimeInterval) {
        lock.lock()
        opener = k
        if timeout > 0 {
            let t = DispatchWorkItem { [weak self] in self?.fail(URLError(.timedOut)) }
            timer = t
            DispatchQueue.global().asyncAfter(deadline: .now() + timeout, execute: t)
        }
        lock.unlock()
        task.resume()
    }

    /// The handshake failed (a timeout, the task cancelled): connect
    /// throws error, and the task goes.
    func fail(_ error: Error) {
        lock.lock()
        let k = opener
        opener = nil
        timer?.cancel()
        timer = nil
        lock.unlock()
        guard let k else { return }
        task.cancel()
        k.resume(throwing: error)
    }

    func opened(_ p: String) {
        lock.lock()
        let k = opener
        opener = nil
        timer?.cancel()
        timer = nil
        isOpen = true
        lock.unlock()
        k?.resume(returning: p)
    }

    func closed(code: Int, reason: Data?) {
        lock.lock()
        if close == nil {
            close = Frame(kind: .closed, text: String(decoding: reason ?? Data(), as: UTF8.self), code: code)
        }
        lock.unlock()
    }

    func completed(_ error: Error?) {
        lock.lock()
        let k = opener
        opener = nil
        timer?.cancel()
        timer = nil
        done = true
        if close == nil, task.closeCode != .invalid {
            close = Frame(kind: .closed, text: String(decoding: task.closeReason ?? Data(), as: UTF8.self), code: task.closeCode.rawValue)
        }
        if close == nil, let error, !ending, isOpen, !Connection.hungUp(error) {
            failure = error
        }
        let wake = asking ? nil : takeReader()
        let answer = wake.map { _ in finish() }
        lock.unlock()
        if let k {
            k.resume(throwing: handshakeError(error))
        }
        if let wake, let answer {
            wake.resume(with: answer)
        }
    }

    /// Whether error is the server hanging up without a close (URLSession
    /// says its socket isn't connected): the end, 1006, not a failure.
    static func hungUp(_ error: Error) -> Bool {
        let e = error as NSError
        return e.domain == NSPOSIXErrorDomain && e.code == Int(ENOTCONN)
    }

    /// Why the handshake failed: the task's error, its HTTP status when
    /// the server answered with one.
    private func handshakeError(_ error: Error?) -> Error {
        if let http = task.response as? HTTPURLResponse, http.statusCode != 101 {
            return URLError(.badServerResponse, userInfo: [NSLocalizedDescriptionKey: "the server answered \(http.statusCode) \(HTTPURLResponse.localizedString(forStatusCode: http.statusCode)), not a WebSocket"])
        }
        return error ?? URLError(.networkConnectionLost)
    }

    private func takeReader() -> CheckedContinuation<Frame, Error>? {
        let r = reader?.k
        reader = nil
        return r
    }

    /// The end, as a read gives it (the lock held): a failure not yet
    /// reported, else the close.
    private func finish() -> Result<Frame, Error> {
        if let f = failure {
            failure = nil
            return .failure(f)
        }
        return .success(close ?? sent ?? Frame(kind: .closed, code: 1006))
    }

        private func nextReader() -> UInt64 {
        lock.lock()
        defer { lock.unlock() }
        readers += 1
        return readers
    }

    func receive() async throws -> Frame {
        let id = nextReader()
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (k: CheckedContinuation<Frame, Error>) in
                lock.lock()
                if Task.isCancelled {
                    lock.unlock()
                    k.resume(throwing: CancellationError())
                    return
                }
                if let r = kept {
                    kept = nil
                    lock.unlock()
                    k.resume(with: r)
                    return
                }
                if done && !asking {
                    let r = finish()
                    lock.unlock()
                    k.resume(with: r)
                    return
                }
                if reader != nil {
                    lock.unlock()
                    k.resume(throwing: URLError(.unknown, userInfo: [NSLocalizedDescriptionKey: "the socket is already being read"]))
                    return
                }
                reader = (id, k)
                let ask = !asking
                asking = true
                lock.unlock()
                if ask { askTask() }
            }
        } onCancel: {
            // The read stops; a message that comes is kept for the next.
            lock.lock()
            let r = reader?.id == id ? takeReader() : nil
            lock.unlock()
            r?.resume(throwing: CancellationError())
        }
    }

    /// Asks the task for its next message.
    private func askTask() {
        task.receive { [self] result in
            lock.lock()
            asking = false
            var answer: Result<Frame, Error>
            switch result {
            case .success(.string(let s)):
                answer = .success(Frame(kind: .text, text: s))
            case .success(.data(let d)):
                answer = .success(Frame(kind: .binary, data: d))
            case .success:
                answer = .success(Frame(kind: .binary))
            case .failure:
                // The connection's over: its close, if the task has it
                // now; else what its end says, once it's done.
                if close == nil, task.closeCode != .invalid {
                    close = Frame(kind: .closed, text: String(decoding: task.closeReason ?? Data(), as: UTF8.self), code: task.closeCode.rawValue)
                }
                if !done && close == nil && sent == nil {
                    lock.unlock()
                    return  // completed answers the reader
                }
                answer = finish()
            }
            if let r = takeReader() {
                lock.unlock()
                r.resume(with: answer)
            } else {
                if case .success(let f) = answer, f.kind != .closed {
                    kept = answer
                } else if case .failure(let e) = answer {
                    failure = e  // for the next read
                }
                lock.unlock()
            }
        }
    }

    func shutdown(code: Int, reason: String) {
        lock.lock()
        ending = true
        if sent == nil {
            sent = Frame(kind: .closed, text: reason, code: code)
        }
        lock.unlock()
        let c = URLSessionWebSocketTask.CloseCode(rawValue: code) ?? .normalClosure
        task.cancel(with: c, reason: reason.isEmpty ? nil : Data(reason.utf8))
    }

    /// Lets the connection go: closed normally if it's open, a reader
    /// waiting answered with that close.
    func letGo() {
        lock.lock()
        let wasOpen = isOpen && !done
        ending = true
        if close == nil {
            close = Frame(kind: .closed, code: 1000)
        }
        done = true
        let r = takeReader()
        let answer = finish()
        lock.unlock()
        if wasOpen {
            task.cancel(with: .normalClosure, reason: nil)
        } else {
            task.cancel()
        }
        r?.resume(with: answer)
    }

    func send(_ m: URLSessionWebSocketTask.Message) async throws {
        try await withCheckedThrowingContinuation { (k: CheckedContinuation<Void, Error>) in
            task.send(m) { error in
                if let error {
                    k.resume(throwing: error)
                } else {
                    k.resume()
                }
            }
        }
    }
}

/// What connect gives Asuka. Let go, it closes the connection (the
/// session holds the connection's state until its task ends).
private final class URLSessionSocket: Socket {
    let connection: Connection
    let chosen: String

    init(_ connection: Connection, protocol p: String) {
        self.connection = connection
        self.chosen = p
    }

    deinit {
        connection.letGo()
    }

    func `protocol`() throws -> String { chosen }

    func receive() async throws -> Frame {
        try await connection.receive()
    }

    func sendText(text: String) async throws {
        try await connection.send(.string(text))
    }

    func sendBinary(data: Data) async throws {
        try await connection.send(.data(data))
    }

    func shutdown(code: Int, reason: String) throws {
        connection.shutdown(code: code, reason: reason)
    }

    func close() throws {
        connection.letGo()
    }
}
