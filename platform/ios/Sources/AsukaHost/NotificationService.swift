#if canImport(UIKit) && ASUKA_NOTIFICATION_SERVICE // an app that uses push.rewrite (asuka/push's manifest)
import UIKit
import UserNotifications
#if canImport(CAsuka)
@_implementationOnly import CAsuka
#endif

/// The Notification Service Extension of an app with a push rewrite
/// (asuka/push's rewrite): its principal class, which runs the app in the
/// extension without a screen (ASUKA_NOTIFICATION_SERVICE: asuka/platform's
/// process is "notification", push.inExtension() true). Each push with
/// mutable-content is handed to the rewrite the app sets (push.rewrite);
/// what it answers becomes the notification (AsukaNotifyContent: the
/// image attached, a message as a communication notification). If the
/// rewrite fails, or the time iOS gives runs out, the push shows as it
/// came.
@objc(AsukaNotificationService)
public final class AsukaNotificationService: UNNotificationServiceExtension {
    private var token = ""

    public override func didReceive(_ request: UNNotificationRequest, withContentHandler contentHandler: @escaping (UNNotificationContent) -> Void) {
        DispatchQueue.main.async {
            MainActor.assumeIsolated { self.token = AsukaRewrites.shared.start(request, contentHandler) }
        }
    }

    public override func serviceExtensionTimeWillExpire() {
        DispatchQueue.main.async {
            MainActor.assumeIsolated { AsukaRewrites.shared.finish(self.token, nil) }
        }
    }
}

/// The rewrites waiting for the app, and push's methods for them:
/// "nextRewrite" answers with one ({token, message: push.Message's JSON}),
/// "rewritten" ({token, ok, spec}) answers it; "stop" ("nextRewrite")
/// fails the waiting call, others go to stop (push's own).
@MainActor final class AsukaRewrites {
    static let shared = AsukaRewrites()
    private let queue = AsukaWaitQueue()
    private var pending: [String: (request: UNNotificationRequest, handler: (UNNotificationContent) -> Void)] = [:]

    func methods(stop: AsukaModuleMethod?) -> [String: AsukaModuleMethod] {
        [
            "nextRewrite": { [unowned self] _, reply in self.queue.next(reply) },
            "stop": { [unowned self] args, reply in
                guard String(decoding: args, as: UTF8.self) == "nextRewrite" else {
                    if let stop { stop(args, reply) } else { reply(.success(Data())) }
                    return
                }
                self.queue.stop()
                reply(.success(Data()))
            },
            "rewritten": { [unowned self] args, reply in
                reply(.success(Data()))
                guard let o = (try? JSONSerialization.jsonObject(with: args)) as? [String: Any],
                      let token = o["token"] as? String, let p = self.pending[token] else { return }
                guard o["ok"] as? Bool == true, let spec = o["spec"] as? [String: Any],
                      let base = p.request.content.mutableCopy() as? UNMutableNotificationContent else {
                    self.finish(token, nil)
                    return
                }
                AsukaNotifyContent.make(spec, into: base) { content in self.finish(token, content) }
            },
        ]
    }

    /// Hands request to the rewrite, starting the app if it hasn't;
    /// returns its token.
    func start(_ request: UNNotificationRequest, _ handler: @escaping (UNNotificationContent) -> Void) -> String {
        "ASUKA_NOTIFICATION_SERVICE".withCString { k in "1".withCString { v in asuka_setenv(k, v) } }
        AsukaRuntime.shared.startHeadless()
        let token = UUID().uuidString
        pending[token] = (request, handler)
        let content = request.content
        let message = AsukaNotifyContent.pushMessage(content.userInfo, id: request.identifier, title: content.title, body: content.body)
        var o: [String: Any] = ["token": token]
        o["message"] = try? JSONSerialization.jsonObject(with: message)
        queue.deliver((try? JSONSerialization.data(withJSONObject: o)) ?? Data())
        return token
    }

    /// Shows the rewrite's content, or the push as it came (nil): once.
    func finish(_ token: String, _ content: UNNotificationContent?) {
        guard let p = pending.removeValue(forKey: token) else { return }
        p.handler(content ?? p.request.content)
    }
}
#endif
