// asuka/push on a Mac: APNs, as on iOS. "register" asks for notifications
// (the user notification center: the app's bundle), registers with APNs
// (NSApplication's registerForRemoteNotifications) and answers JSON
// (token, service, allowed); "nextReceived" and "nextOpened" answer with
// the next message (push.Message's JSON), kept until asked; "stop" (the
// method's name) fails the waiting ones with "stopped". The center's
// delegate (Notify.swift) hands it the pushes shown and tapped; the
// application delegate, the data messages. APNs needs the app signed with
// the aps-environment entitlement, which only a provisioning profile
// grants: without it, registering fails with APNs's error. A push's
// rewrite needs a notification service extension, which a Mac app from
// asuka build doesn't have.
import AppKit
import UserNotifications

@MainActor final class AsukaPush {
    static let shared = AsukaPush()
    /// The register calls APNs's answer goes to, and whether the user
    /// allows notifications (internal: the unit tests set them).
    var registering: [(Result<Data, Error>) -> Void] = []
    var allowed = false
    private let queues: [String: AsukaNotifyQueue] = ["nextReceived": AsukaNotifyQueue(), "nextOpened": AsukaNotifyQueue()]

    func methods() -> [String: AsukaModuleMethod] {
        [
            "register": { [unowned self] _, reply in
                guard AsukaNotify.bundled else {
                    reply(.failure(AsukaModuleError("asuka/push: APNs needs the app's bundle: build it with asuka build")))
                    return
                }
                AsukaNotify.install()
                guard let center = AsukaNotify.shared.center else {
                    reply(.failure(AsukaModuleError("asuka/push: no user notification center")))
                    return
                }
                center.requestAuthorization(options: [.alert, .sound, .badge]) { granted, _ in
                    DispatchQueue.main.async {
                        MainActor.assumeIsolated {
                            self.allowed = granted
                            self.registering.append(reply)
                            NSApp.registerForRemoteNotifications()
                        }
                    }
                }
            },
            "nextReceived": { [unowned self] _, reply in self.queues["nextReceived"]?.next(reply) },
            "nextOpened": { [unowned self] _, reply in self.queues["nextOpened"]?.next(reply) },
            "stop": { [unowned self] args, reply in
                self.queues[String(decoding: args, as: UTF8.self)]?.stop()
                reply(.success(Data()))
            },
            "nextRewrite": { _, reply in
                reply(.failure(AsukaModuleError("asuka/push: rewriting needs a notification service extension, which a Mac app doesn't have")))
            },
            "rewritten": { _, reply in reply(.success(Data())) },
        ]
    }

    func deliver(_ kind: String, _ m: Data) { queues[kind]?.deliver(m) }

    /// The automation's JSON of it (Modules1.swift's modules1JSON).
    func json() -> [String: Any] {
        ["registering": registering.count, "allowed": allowed,
         "received": queues["nextReceived"]?.json() ?? [:], "opened": queues["nextOpened"]?.json() ?? [:]]
    }

    // MARK: From the application's delegate

    /// APNs's answer to registering: the device token.
    func registered(_ token: Data) {
        let hex = token.map { String(format: "%02x", $0) }.joined()
        let d = (try? JSONSerialization.data(withJSONObject: ["token": hex, "service": "apns", "allowed": allowed] as [String: Any], options: [.sortedKeys])) ?? Data()
        let rs = registering
        registering = []
        for r in rs { r(.success(d)) }
    }

    /// Why APNs didn't register the app.
    func failed(_ error: Error) {
        let msg = error.localizedDescription
        let rs = registering
        registering = []
        for r in rs { r(.failure(AsukaModuleError(msg.isEmpty ? "APNs refused the registration" : msg))) }
    }

    /// A data message (content-available) as the app runs.
    func received(_ userInfo: [AnyHashable: Any]) {
        deliver("nextReceived", Self.message(userInfo, id: ""))
    }

    // MARK: From the center's delegate

    /// A push shown as the app runs.
    func shown(_ n: UNNotification) {
        guard n.request.trigger is UNPushNotificationTrigger else { return }
        let c = n.request.content
        deliver("nextReceived", Self.message(c.userInfo, id: n.request.identifier, title: c.title, body: c.body))
    }

    /// A push tapped (or its action chosen), not dismissed.
    func opened(_ r: UNNotificationResponse) {
        let n = r.notification
        guard n.request.trigger is UNPushNotificationTrigger, r.actionIdentifier != UNNotificationDismissActionIdentifier else { return }
        let c = n.request.content
        deliver("nextOpened", Self.message(c.userInfo, id: n.request.identifier, title: c.title, body: c.body))
    }

    // MARK: Messages

    /// A push's data: its payload's keys beside aps, as strings (numbers
    /// and booleans as JSON writes them, the rest as JSON, its keys sorted
    /// as the Go host's encoding/json wrote them).
    nonisolated static func data(_ userInfo: [AnyHashable: Any]) -> [String: String] {
        var data: [String: String] = [:]
        for (k, v) in userInfo {
            guard let k = k as? String, k != "aps" else { continue }
            if let s = v as? String {
                data[k] = s
            } else if let n = v as? NSNumber {
                data[k] = CFGetTypeID(n) == CFBooleanGetTypeID() ? (n.boolValue ? "true" : "false") : n.stringValue
            } else {
                data[k] = (try? JSONSerialization.data(withJSONObject: v, options: [.fragmentsAllowed, .sortedKeys, .withoutEscapingSlashes])).map { String(decoding: $0, as: UTF8.self) } ?? ""
            }
        }
        return data
    }

    /// A push as push.Message's JSON: its id, title, body, data, and what
    /// its aps says (subtitle, category, thread-id, badge).
    nonisolated static func message(_ userInfo: [AnyHashable: Any], id: String, title: String = "", body: String = "") -> Data {
        let aps = userInfo["aps"] as? [String: Any] ?? [:]
        var t = title, b = body, sub = ""
        if let s = aps["alert"] as? String {
            if b.isEmpty { b = s }
        } else if let a = aps["alert"] as? [String: Any] {
            if t.isEmpty && b.isEmpty {
                t = a["title"] as? String ?? ""
                b = a["body"] as? String ?? ""
            }
            sub = a["subtitle"] as? String ?? ""
        }
        let o: [String: Any] = ["id": id, "title": t, "body": b, "subtitle": sub, "data": data(userInfo),
                                "category": aps["category"] as? String ?? "", "thread": aps["thread-id"] as? String ?? "",
                                "badge": (aps["badge"] as? NSNumber)?.intValue ?? 0]
        return (try? JSONSerialization.data(withJSONObject: o)) ?? Data("{}".utf8)
    }
}

// The application delegate's part of APNs.
extension AsukaAppDelegate {
    func application(_ app: NSApplication, didRegisterForRemoteNotificationsWithDeviceToken token: Data) {
        AsukaPush.shared.registered(token)
    }

    func application(_ app: NSApplication, didFailToRegisterForRemoteNotificationsWithError error: Error) {
        AsukaPush.shared.failed(error)
    }

    func application(_ app: NSApplication, didReceiveRemoteNotification userInfo: [String: Any]) {
        AsukaPush.shared.received(userInfo)
    }
}
