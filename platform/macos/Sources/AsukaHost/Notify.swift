// asuka/notify on macOS: UNUserNotificationCenter, as the iOS host has it
// (platform/ios's Notify.swift), the module's methods the same. What macOS
// lacks of it: Android's channels (ignored, as on iOS) and communication
// notifications (a message shows as a notification, its sender's name its
// title). Pushes (APNs) shown or tapped go to asuka/push too (Push.swift),
// their data the payload's keys beside aps. The center needs the app's
// bundle: a process with no bundle identifier gets an error (the center
// raises an exception for it).
//
// The module ("notify"):
// - "request" (u8 allowed); "schedule" (a std/notify's Spec: seconds 0
//   shows it now); "cancel" (the id).
// - "categories" (JSON), kept in the user defaults and set again at launch.
// - "badge" (u32: the Dock tile's); "showing" (JSON list); "remove" (JSON
//   ids), "removeThread" (the thread), "removeAll".
// - "present" (u8 on), "nextPresent" (a std/notify's Present), "presented"
//   (u8 presentation, the token): with the presenter on, a notification
//   that comes while the app is in front waits for the app's answer, a
//   second at most.
// - "nextAction" (a std/notify's WireResponse), "actionDone" (the token): a
//   background action keeps the app's work going until the app is done
//   with it (25 seconds at most); a tap or an action that opens the app is
//   done at once.
// - "stop" (the method's name); "channels", "channelGroups": Android's.
import AppKit
import UserNotifications
import UniformTypeIdentifiers
#if canImport(AsukaOps)
import AsukaOps
#endif

/// What the app waits for (the "next..." methods): items are kept until
/// asked, waiting calls until an item comes; stop fails the waiting ones
/// with "stopped".
@MainActor final class AsukaNotifyQueue {
    private var items: [Data] = []
    private var waiting: [(Result<Data, Error>) -> Void] = []

    func next(_ reply: @escaping (Result<Data, Error>) -> Void) {
        if items.isEmpty { waiting.append(reply) } else { reply(.success(items.removeFirst())) }
    }

    func deliver(_ item: Data) {
        if waiting.isEmpty { items.append(item) } else { waiting.removeFirst()(.success(item)) }
    }

    /// What's kept and what waits, for the automation's JSON.
    func json() -> [String: Any] { ["kept": items.count, "waiting": waiting.count] }

    func stop() {
        let ws = waiting
        waiting = []
        for w in ws { w(.failure(AsukaModuleError("stopped"))) }
    }
}

/// The notify module, and the notification center's delegate (asuka/push's
/// notifications too).
@MainActor final class AsukaNotify: NSObject, UNUserNotificationCenterDelegate {
    static let shared = AsukaNotify()
    static let categoriesKey = "asuka.notify.categories"
    /// The seconds a background action has, as on iOS.
    static let actionSeconds = 25

    private var tried = false
    /// The delegate was installed before the application ran (the
    /// automation's JSON shows it).
    private var installedBeforeLaunch = false
    /// The center, once the delegate is installed (nil: no bundle).
    private(set) var center: UNUserNotificationCenter?
    private var presenting = false
    private let presents = AsukaNotifyQueue(), actions = AsukaNotifyQueue()
    private var pendingPresent: [String: (UNNotificationPresentationOptions) -> Void] = [:]
    private var pendingActions: [String: () -> Void] = [:]
    private var categories = Data()
    private var tokens = 0

    /// Whether the process is an app bundle, which the center needs.
    static var bundled: Bool { Bundle.main.bundleIdentifier != nil }

    static let noBundle = AsukaModuleError("asuka/notify: macOS's notifications need the app's bundle: build it with asuka build")

    /// Becomes the center's delegate, and sets the app's categories, if the
    /// app is a bundle: before it finishes launching, to see a response
    /// that launched it.
    static func install() { shared.installCenter() }

    private func installCenter() {
        guard !tried else { return }
        tried = true
        guard Self.bundled else { return }
        let c = UNUserNotificationCenter.current()
        c.delegate = self
        if let d = UserDefaults.standard.data(forKey: Self.categoriesKey) {
            categories = d
            c.setNotificationCategories(Self.categorySet(d))
        }
        center = c
        installedBeforeLaunch = !NSApp.isRunning
    }

    /// The automation's JSON of it (Modules1.swift's modules1JSON).
    func json() -> [String: Any] {
        ["bundled": Self.bundled, "delegate": center.map { $0.delegate === self } ?? false,
         "installedBeforeLaunch": installedBeforeLaunch, "badge": NSApp.dockTile.badgeLabel ?? "",
         "categories": String(decoding: categories, as: UTF8.self), "presenting": presenting,
         "pendingPresent": pendingPresent.count, "pendingActions": pendingActions.count,
         "presents": presents.json(), "actions": actions.json()]
    }

    /// The center, installing the delegate; nil (and the call failed) if
    /// the app has no bundle.
    private func center(_ reply: (Result<Data, Error>) -> Void) -> UNUserNotificationCenter? {
        installCenter()
        guard let c = center else {
            reply(.failure(Self.noBundle))
            return nil
        }
        return c
    }

    private func token() -> String {
        tokens += 1
        let f = DateFormatter()
        f.dateFormat = "HHmmss.SSSSSS"
        return f.string(from: Date()) + "-\(tokens)"
    }

    func methods() -> [String: AsukaModuleMethod] {
        let ok: AsukaModuleMethod = { _, reply in reply(.success(Data())) }
        let done: (@escaping (Result<Data, Error>) -> Void) -> (Error?) -> Void = { reply in
            { error in reply(error.map { .failure(AsukaModuleError($0.localizedDescription)) } ?? .success(Data())) }
        }
        return [
            "request": { [unowned self] _, reply in
                guard let c = self.center(reply) else { return }
                c.requestAuthorization(options: [.alert, .sound, .badge]) { granted, error in
                    if let error { reply(.failure(AsukaModuleError(error.localizedDescription))) } else { reply(.success(Data([granted ? 1 : 0]))) }
                }
            },
            "schedule": { [unowned self] args, reply in
                guard let c = self.center(reply) else { return }
                guard let o = (try? JSONSerialization.jsonObject(with: args)) as? [String: Any] else {
                    reply(.failure(AsukaModuleError("bad notification")))
                    return
                }
                var id = o["id"] as? String ?? ""
                if id.isEmpty {
                    let f = DateFormatter()
                    f.dateFormat = "yyyyMMddHHmmss.SSSSSSSSS"
                    id = f.string(from: Date())
                }
                let after = (o["seconds"] as? NSNumber)?.doubleValue ?? 0
                let add: ((URL, String)?) -> Void = { image in
                    let trigger = after > 0 ? UNTimeIntervalNotificationTrigger(timeInterval: max(1, after), repeats: false) : nil
                    let req = UNNotificationRequest(identifier: id, content: Self.content(o, image), trigger: trigger)
                    c.add(req, withCompletionHandler: done(reply))
                }
                guard let src = o["image"] as? String, !src.isEmpty else { add(nil); return }
                Self.fetch(src, done: add)
            },
            "cancel": { [unowned self] args, reply in
                guard let c = self.center(reply) else { return }
                let id = String(decoding: args, as: UTF8.self)
                c.removePendingNotificationRequests(withIdentifiers: [id])
                c.removeDeliveredNotifications(withIdentifiers: [id])
                reply(.success(Data()))
            },
            "categories": { [unowned self] args, reply in
                guard let c = self.center(reply) else { return }
                self.categories = args
                UserDefaults.standard.set(args, forKey: Self.categoriesKey)
                c.setNotificationCategories(Self.categorySet(args))
                reply(.success(Data()))
            },
            // The Dock's badge: the app's icon there.
            "badge": { args, reply in
                var r = AsukaByteReader(args)
                let n = args.count >= 4 ? Int(r.u32()) : 0
                NSApp.dockTile.badgeLabel = n > 0 ? String(n) : ""
                reply(.success(Data()))
            },
            "showing": { [unowned self] _, reply in
                guard let c = self.center(reply) else { return }
                c.getDeliveredNotifications { ns in
                    DispatchQueue.main.async {
                        // Newest first; those of the same time as they came.
                        let list = ns.enumerated().sorted {
                            $0.element.date != $1.element.date ? $0.element.date > $1.element.date : $0.offset < $1.offset
                        }.map { Self.shown($0.element) }
                        reply(.success((try? JSONSerialization.data(withJSONObject: list)) ?? Data("[]".utf8)))
                    }
                }
            },
            "remove": { [unowned self] args, reply in
                guard let c = self.center(reply) else { return }
                c.removeDeliveredNotifications(withIdentifiers: (try? JSONSerialization.jsonObject(with: args)) as? [String] ?? [])
                reply(.success(Data()))
            },
            "removeThread": { [unowned self] args, reply in
                guard let c = self.center(reply) else { return }
                let thread = String(decoding: args, as: UTF8.self)
                c.getDeliveredNotifications { ns in
                    c.removeDeliveredNotifications(withIdentifiers: ns.filter { $0.request.content.threadIdentifier == thread }.map { $0.request.identifier })
                    reply(.success(Data()))
                }
            },
            "removeAll": { [unowned self] _, reply in
                guard let c = self.center(reply) else { return }
                c.removeAllDeliveredNotifications()
                reply(.success(Data()))
            },
            "present": { [unowned self] args, reply in
                self.presenting = args.first == 1
                if !self.presenting {
                    let ps = self.pendingPresent
                    self.pendingPresent = [:]
                    for (_, d) in ps { d(Self.options(0)) }
                }
                reply(.success(Data()))
            },
            "nextPresent": { [unowned self] _, reply in self.presents.next(reply) },
            "presented": { [unowned self] args, reply in
                if let how = args.first, let d = self.pendingPresent.removeValue(forKey: String(decoding: args.dropFirst(), as: UTF8.self)) {
                    d(Self.options(how))
                }
                reply(.success(Data()))
            },
            "nextAction": { [unowned self] _, reply in
                self.installCenter()
                self.actions.next(reply)
            },
            "actionDone": { [unowned self] args, reply in
                self.pendingActions.removeValue(forKey: String(decoding: args, as: UTF8.self))?()
                reply(.success(Data()))
            },
            "stop": { [unowned self] args, reply in
                switch String(decoding: args, as: UTF8.self) {
                case "nextPresent": self.presents.stop()
                case "nextAction": self.actions.stop()
                default: break
                }
                reply(.success(Data()))
            },
            // Android's: macOS has no channels.
            "channels": ok,
            "channelGroups": ok,
        ]
    }

    /// A presentation (notify.Presentation) as the center's options.
    nonisolated static func options(_ how: UInt8) -> UNNotificationPresentationOptions {
        switch how {
        case 1: return [.list, .badge]
        case 2: return []
        default: return [.banner, .list, .sound, .badge]
        }
    }

    /// The categories' JSON (notify.setCategories) as the center's.
    nonisolated static func categorySet(_ d: Data) -> Set<UNNotificationCategory> {
        guard let cs = (try? JSONSerialization.jsonObject(with: d)) as? [[String: Any]] else { return [] }
        var out = Set<UNNotificationCategory>()
        for c in cs {
            let id = c["id"] as? String ?? ""
            var actions: [UNNotificationAction] = []
            for a in c["actions"] as? [[String: Any]] ?? [] {
                let aid = a["id"] as? String ?? "", title = a["title"] as? String ?? ""
                var opts: UNNotificationActionOptions = []
                if a["unlock"] as? Bool == true { opts.insert(.authenticationRequired) }
                if a["destructive"] as? Bool == true { opts.insert(.destructive) }
                if a["open"] as? Bool == true { opts.insert(.foreground) }
                let r = a["reply"] as? [String: Any]
                let icon = a["icon"] as? String ?? ""
                // Icons are macOS 12's.
                if #available(macOS 12.0, *) {
                    let i = icon.isEmpty ? nil : UNNotificationActionIcon(systemImageName: icon)
                    if let r {
                        actions.append(UNTextInputNotificationAction(identifier: aid, title: title, options: opts, icon: i,
                                                                     textInputButtonTitle: r["button"] as? String ?? "",
                                                                     textInputPlaceholder: r["placeholder"] as? String ?? ""))
                    } else {
                        actions.append(UNNotificationAction(identifier: aid, title: title, options: opts, icon: i))
                    }
                } else if let r {
                    actions.append(UNTextInputNotificationAction(identifier: aid, title: title, options: opts,
                                                                 textInputButtonTitle: r["button"] as? String ?? "",
                                                                 textInputPlaceholder: r["placeholder"] as? String ?? ""))
                } else {
                    actions.append(UNNotificationAction(identifier: aid, title: title, options: opts))
                }
            }
            out.insert(UNNotificationCategory(identifier: id, actions: actions, intentIdentifiers: [], options: []))
        }
        return out
    }

    /// Whether category's action opens the app.
    private func opensApp(_ category: String, _ action: String) -> Bool {
        guard let cs = (try? JSONSerialization.jsonObject(with: categories)) as? [[String: Any]] else { return false }
        for c in cs where c["id"] as? String == category {
            for a in c["actions"] as? [[String: Any]] ?? [] where a["id"] as? String == action {
                return a["open"] as? Bool == true
            }
        }
        return false
    }

    /// A spec's notification content; image is its attachment's file and
    /// type.
    nonisolated static func content(_ o: [String: Any], _ image: (URL, String)?) -> UNNotificationContent {
        let c = UNMutableNotificationContent()
        var title = o["title"] as? String ?? ""
        if title.isEmpty, let s = o["sender"] as? [String: Any] { title = s["name"] as? String ?? "" }
        c.title = title
        c.subtitle = o["subtitle"] as? String ?? ""
        c.body = o["body"] as? String ?? ""
        if o["silent"] as? Bool != true { c.sound = .default }
        var info: [AnyHashable: Any] = ["asuka.data": o["data"] as? [String: String] ?? [:]]
        if let u = o["url"] as? String, !u.isEmpty { info["url"] = u }
        c.userInfo = info
        c.categoryIdentifier = o["category"] as? String ?? ""
        c.threadIdentifier = o["thread"] as? String ?? ""
        if let b = (o["badge"] as? NSNumber)?.intValue, b > 0 { c.badge = NSNumber(value: b) }
        if let (file, type) = image,
           let a = try? UNNotificationAttachment(identifier: "image", url: file, options: [UNNotificationAttachmentOptionsTypeHintKey: type]) {
            c.attachments = [a]
        }
        return c
    }

    /// The local copy of an image source (as Image's: an http(s) URL, a
    /// file, an asset), which the system moves into its store, and its
    /// type's identifier; nil if it can't be had (the notification shows
    /// without it). done gets it on the main thread.
    static func fetch(_ src: String, done: @escaping ((URL, String)?) -> Void) {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("asuka-notify", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let finish: @Sendable (URL, String) -> Void = { from, ext in
            let type = UTType(filenameExtension: ext.lowercased())?.identifier ?? UTType.jpeg.identifier
            let to = dir.appendingPathComponent(UUID().uuidString).appendingPathExtension(ext.isEmpty ? "jpg" : ext)
            let ok = (try? FileManager.default.copyItem(at: from, to: to)) != nil
            DispatchQueue.main.async { done(ok ? (to, type) : nil) }
        }
        if src.hasPrefix("http://") || src.hasPrefix("https://"), let u = URL(string: src) {
            var req = URLRequest(url: u)
            req.timeoutInterval = 15
            URLSession.shared.downloadTask(with: req) { file, resp, _ in
                guard let file, (resp as? HTTPURLResponse).map({ (200..<300).contains($0.statusCode) }) ?? true else {
                    DispatchQueue.main.async { done(nil) }
                    return
                }
                var ext = u.pathExtension
                if let mime = resp?.mimeType, let e = UTType(mimeType: mime)?.preferredFilenameExtension { ext = e }
                finish(file, ext)
            }.resume()
            return
        }
        let file: URL
        if src.hasPrefix("file://"), let u = URL(string: src) {
            file = u
        } else if src.hasPrefix("/") {
            file = URL(fileURLWithPath: src)
        } else {
            file = AsukaRuntime.shared.assets.appendingPathComponent(src)
        }
        DispatchQueue.global(qos: .userInitiated).async { finish(file, file.pathExtension) }
    }

    /// What's reported of a notification (a std/notify's WireDelivered).
    nonisolated static func shown(_ n: UNNotification) -> [String: Any] {
        let c = n.request.content
        let remote = n.request.trigger is UNPushNotificationTrigger
        var data: [String: String] = [:]
        if remote {
            data = AsukaPush.data(c.userInfo)
        } else if let d = c.userInfo["asuka.data"] as? [String: Any] {
            for (k, v) in d { data[k] = v as? String ?? "\(v)" }
        }
        return ["id": n.request.identifier, "title": c.title, "subtitle": c.subtitle, "body": c.body,
                "url": c.userInfo["url"] as? String ?? "", "data": data, "category": c.categoryIdentifier,
                "thread": c.threadIdentifier, "time": Int64(n.date.timeIntervalSince1970 * 1000), "remote": remote]
    }

    // MARK: The center's delegate

    nonisolated func userNotificationCenter(_ c: UNUserNotificationCenter, willPresent n: UNNotification,
                                            withCompletionHandler done: @escaping (UNNotificationPresentationOptions) -> Void) {
        DispatchQueue.main.async {
            MainActor.assumeIsolated { AsukaNotify.shared.willPresent(n, done) }
        }
    }

    private func willPresent(_ n: UNNotification, _ done: @escaping (UNNotificationPresentationOptions) -> Void) {
        AsukaPush.shared.shown(n)
        guard presenting else { done(Self.options(0)); return }
        let t = token()
        pendingPresent[t] = done
        let o: [String: Any] = ["token": t, "notification": Self.shown(n)]
        presents.deliver((try? JSONSerialization.data(withJSONObject: o)) ?? Data())
        // A second at most: then it shows.
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
            MainActor.assumeIsolated {
                AsukaNotify.shared.pendingPresent.removeValue(forKey: t)?(Self.options(0))
            }
        }
    }

    nonisolated func userNotificationCenter(_ c: UNUserNotificationCenter, didReceive r: UNNotificationResponse,
                                            withCompletionHandler done: @escaping () -> Void) {
        DispatchQueue.main.async {
            MainActor.assumeIsolated { AsukaNotify.shared.didReceive(r, done) }
        }
    }

    private func didReceive(_ r: UNNotificationResponse, _ done: @escaping () -> Void) {
        AsukaPush.shared.opened(r)
        let n = r.notification
        let content = n.request.content
        let action = r.actionIdentifier == UNNotificationDefaultActionIdentifier ? "" : r.actionIdentifier
        let t = token()
        let o: [String: Any] = ["token": t, "action": action, "reply": (r as? UNTextInputNotificationResponse)?.userText ?? "",
                                "seconds": Self.actionSeconds, "notification": Self.shown(n)]
        actions.deliver((try? JSONSerialization.data(withJSONObject: o)) ?? Data())
        if action.isEmpty, let u = content.userInfo["url"] as? String, !u.isEmpty {
            // The app's openURL, as a link it's opened with (Files.swift's:
            // a tap that launched the app waits for its root).
            AsukaShell.shared.incoming(u)
        }
        // The app is in front for a tap or an action that opens it: done.
        // A background action keeps its work going until the app is done.
        if action.isEmpty || opensApp(content.categoryIdentifier, action) { done(); return }
        pendingActions[t] = done
        DispatchQueue.main.asyncAfter(deadline: .now() + .seconds(Self.actionSeconds)) {
            MainActor.assumeIsolated { AsukaNotify.shared.pendingActions.removeValue(forKey: t)?() }
        }
    }
}
