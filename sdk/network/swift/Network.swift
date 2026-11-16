// platform/network on iOS and macOS: NWPathMonitor.
import Foundation
import Network

final class PathMonitor: Monitor {
    func current() throws -> Status {
        // A monitor's first path comes soon after it starts.
        let monitor = NWPathMonitor()
        let ready = DispatchSemaphore(value: 0)
        let first = First()
        monitor.pathUpdateHandler = { p in
            if first.path == nil {
                first.path = p
                ready.signal()
            }
        }
        monitor.start(queue: DispatchQueue(label: "asuka.network.current"))
        _ = ready.wait(timeout: .now() + 1)
        monitor.cancel()
        return first.path.map(Self.status) ?? Status(connected: true, type: .other)
    }

    func watch(changed: @escaping (Status) -> Void) async throws {
        let monitor = NWPathMonitor()
        let last = Last()
        let report = Report(changed)
        monitor.pathUpdateHandler = { p in
            // On the monitor's queue, one path at a time.
            let s = Self.status(p)
            if let l = last.status, Self.same(l, s) { return }
            last.status = s
            DispatchQueue.main.async { report.send(s) }
        }
        monitor.start(queue: DispatchQueue(label: "asuka.network"))
        defer { monitor.cancel() }
        // Until the task is cancelled.
        while !Task.isCancelled {
            try? await Task.sleep(nanoseconds: 3_600_000_000_000)
        }
    }

    /// The last status, touched on the monitor's queue only.
    private final class Last: @unchecked Sendable { var status: Status? }

    /// changed, called on the main thread only.
    private final class Report: @unchecked Sendable {
        let send: (Status) -> Void
        init(_ send: @escaping (Status) -> Void) { self.send = send }
    }

    static func status(_ p: NWPath) -> Status {
        guard p.status == .satisfied else { return Status() }
        let type: ConnectionType = p.usesInterfaceType(.wifi) ? .wifi
            : p.usesInterfaceType(.cellular) ? .cellular
            : p.usesInterfaceType(.wiredEthernet) ? .ethernet : .other
        return Status(connected: true, type: type, metered: p.isExpensive, constrained: p.isConstrained)
    }

    /// The first path, set on the monitor's queue before the wait ends.
    private final class First: @unchecked Sendable { var path: NWPath? }

    static func same(_ a: Status, _ b: Status) -> Bool {
        a.connected == b.connected && a.type == b.type && a.metered == b.metered && a.constrained == b.constrained
    }
}

func makeMonitor() throws -> any Monitor { PathMonitor() }
