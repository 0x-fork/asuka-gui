// What the app opens, its recent documents and its login item, on a Mac.
//
// Files: Launch Services opens the app with them (a double click in
// Finder, Open With, a file dropped on its Dock icon) through
// application(_:open:), together with links of its scheme; the files go to
// the core as file:// URLs, one per line (internal/fileurl), so that the
// app's on openFiles gets those opened together at once.
//
// Recent documents are NSDocumentController's, which keeps them across
// launches and lists them in the Dock menu; the File menu's Open Recent
// lists them too, and choosing one opens it as Finder would.
//
// The login item is SMAppService's mainApp (macOS 13).
import AppKit
import ServiceManagement
#if canImport(AsukaOps)
import AsukaOps
#endif

/// What the shell keeps of the files: the URLs opened before the app
/// showed, the recent documents' menu, the launch.
@MainActor final class AsukaFiles: NSObject, NSMenuDelegate {
    static let shared = AsukaFiles()
    /// IncomingURL strings waiting for the root (opened as the app launched,
    /// before the core had made it).
    var pending: [String] = []
    /// recent.add was called: the File menu has Open Recent.
    var recentUsed = false
    /// Open Recent's submenu, filled as it opens.
    weak var recentMenu: NSMenu?
    /// The launch's Apple Event has been read.
    var launched = false
    var atLogin = false
    var loginWaiting: [(Result<Data, Error>) -> Void] = []

    func menuNeedsUpdate(_ menu: NSMenu) { fillRecent(menu) }

    @objc func openRecent(_ sender: NSMenuItem) {
        if let u = sender.representedObject as? URL { AsukaShell.shared.openURLs([u]) }
    }

    @objc func clearRecent(_ sender: Any?) { NSDocumentController.shared.clearRecentDocuments(nil) }

    @objc func appLaunched(_ note: Notification) { launchedNow() }

    /// The app finished launching: the Apple Event that opened it says
    /// whether it opened as a login item.
    func launchedNow() {
        if launched { return }
        launched = true
        let keyAEPropData: AEKeyword = 0x7072_6474 // 'prdt'
        let keyAELaunchedAsLogInItem: OSType = 0x6c67_6974 // 'lgit'
        let kAEOpenApplication: AEEventID = 0x6f61_7070 // 'oapp'
        if let ev = NSAppleEventManager.shared().currentAppleEvent, ev.eventID == kAEOpenApplication,
           let p = ev.paramDescriptor(forKeyword: keyAEPropData) {
            atLogin = p.enumCodeValue == keyAELaunchedAsLogInItem
        }
        // Windows' and Linux's login entries start the app with an argument
        // (--asuka-opened-at-login); a Mac app started so by hand reads it too.
        atLogin = atLogin || CommandLine.arguments.contains("--asuka-opened-at-login")
        let r = Data([atLogin ? 1 : 0])
        let ws = loginWaiting
        loginWaiting = []
        for reply in ws { reply(.success(r)) }
    }

    /// Open Recent's items, made as it opens: the recent documents that
    /// still exist, with their icons and, where two have one name, their
    /// folders', then Clear Menu.
    func fillRecent(_ m: NSMenu) {
        m.removeAllItems()
        let paths = recentPaths()
        let fm = FileManager.default
        let names = paths.map { fm.displayName(atPath: $0) }
        var count: [String: Int] = [:]
        for n in names { count[n, default: 0] += 1 }
        for (i, p) in paths.enumerated() {
            var title = names[i]
            if count[title, default: 0] > 1 {
                title += " — " + fm.displayName(atPath: (p as NSString).deletingLastPathComponent)
            }
            let it = m.addItem(withTitle: title, action: #selector(openRecent(_:)), keyEquivalent: "")
            it.target = self
            it.representedObject = URL(fileURLWithPath: p)
            let icon = NSWorkspace.shared.icon(forFile: p).copy() as! NSImage
            icon.size = CGSize(width: 16, height: 16)
            it.image = icon
        }
        if !paths.isEmpty { m.addItem(.separator()) }
        let clear = m.addItem(withTitle: AsukaShell.shared.menuWord("Clear Menu"), action: #selector(clearRecent(_:)), keyEquivalent: "")
        clear.target = self
        clear.isEnabled = !paths.isEmpty
    }

    /// The recent documents that still exist, newest first.
    func recentPaths() -> [String] {
        NSDocumentController.shared.recentDocumentURLs.compactMap { u in
            u.isFileURL && FileManager.default.fileExists(atPath: u.path) ? u.path : nil
        }
    }
}

@MainActor extension AsukaShell {
    /// As the app starts: the launch's Apple Event, read once it has
    /// finished launching.
    func startFiles() {
        NotificationCenter.default.addObserver(AsukaFiles.shared, selector: #selector(AsukaFiles.appLaunched(_:)),
            name: NSApplication.didFinishLaunchingNotification, object: nil)
    }

    /// application(_:open:): the files among urls (their paths, as file://
    /// URLs again) and its other URLs, in one IncomingURL event.
    func openURLs(_ urls: [URL]) {
        var lines: [String] = []
        for u in urls {
            if u.isFileURL {
                if !u.path.isEmpty { lines.append(asukaFileURL(u.path)) }
                continue
            }
            lines.append(u.absoluteString)
        }
        guard !lines.isEmpty else { return }
        incoming(lines.joined(separator: "\n"))
    }

    /// Sends IncomingURL to the root; before there's one (the app opened
    /// with files as it launched), once there is.
    func incoming(_ s: String) {
        if let r = rt.root, rt.started {
            rt.send(AsukaEvents.incomingURL(r.id, s))
            return
        }
        AsukaFiles.shared.pending.append(s)
        if AsukaFiles.shared.pending.count == 1 { waitForRoot() }
    }

    private func waitForRoot() {
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) {
            MainActor.assumeIsolated {
                let f = AsukaFiles.shared
                guard let r = self.rt.root, self.rt.started else { return self.waitForRoot() }
                let ps = f.pending
                f.pending = []
                for s in ps { self.rt.send(AsukaEvents.incomingURL(r.id, s)) }
            }
        }
    }

    // MARK: Open Recent

    /// Whether the File menu has Open Recent: the app opens files (its
    /// bundle declares document types, asuka.toml's fileTypes), keeps
    /// recent documents (recent.add), or has some from before.
    func hasOpenRecent() -> Bool {
        if AsukaFiles.shared.recentUsed { return true }
        if Bundle.main.object(forInfoDictionaryKey: "CFBundleDocumentTypes") != nil { return true }
        if !rt.config.fileTypes.isEmpty { return true }
        return !NSDocumentController.shared.recentDocumentURLs.isEmpty
    }

    /// Adds Open Recent to the File menu (filled as it opens).
    func addOpenRecent(_ file: NSMenu) {
        let title = menuWord("Open Recent")
        let m = NSMenu(title: title)
        m.autoenablesItems = false
        m.delegate = AsukaFiles.shared
        AsukaFiles.shared.recentMenu = m
        AsukaFiles.shared.fillRecent(m)
        asukaAddSubmenu(file, title, m)
    }
}

/// path's file:// URL: absolute, escaped (fileurl.FromPath).
func asukaFileURL(_ path: String) -> String {
    var c = URLComponents()
    c.scheme = "file"
    c.host = ""
    c.path = path.hasPrefix("/") ? path : "/" + path
    return c.string ?? "file://" + path
}

@MainActor extension AsukaRuntime {
    /// The app's modules (the shell's own): tray, dock, hotkey, apps,
    /// recent, login. host/darwin's dialog and shell modules aren't here:
    /// asuka/dialog and asuka/shell have their own Swift.
    func registerAppModules() {
        registerModule("tray", AsukaTray.shared.methods())
        registerModule("dock", AsukaDock.methods())
        registerModule("hotkey", AsukaHotkeys.shared.methods())
        registerModule("apps", AsukaApps.methods())
        registerModule("recent", recentMethods())
        registerModule("login", loginMethods())
    }

    func recentMethods() -> [String: AsukaModuleMethod] {
        [
            "add": { args, reply in
                let path = String(decoding: args, as: UTF8.self)
                guard path.hasPrefix("/") else { return reply(.failure(AsukaModuleError("asuka/recent: not an absolute path"))) }
                let dc = NSDocumentController.shared
                dc.noteNewRecentDocumentURL(URL(fileURLWithPath: path))
                // Read back at once: AppKit coalesces the notes made before
                // it reads its list, and a document noted again then keeps
                // its place.
                _ = dc.recentDocumentURLs
                let f = AsukaFiles.shared
                if !f.recentUsed {
                    f.recentUsed = true
                    if f.recentMenu == nil { AsukaShell.shared.makeMenuBar() } // the File menu without Open Recent
                }
                reply(.success(Data()))
            },
            "clear": { _, reply in
                NSDocumentController.shared.clearRecentDocuments(nil)
                reply(.success(Data()))
            },
            "list": { _, reply in
                reply(Result { try JSONSerialization.data(withJSONObject: AsukaFiles.shared.recentPaths()) })
            },
        ]
    }

    // MARK: The login item

    func loginMethods() -> [String: AsukaModuleMethod] {
        [
            "set": { args, reply in
                guard #available(macOS 13.0, *) else {
                    return reply(.failure(AsukaModuleError("asuka/login: login items need macOS 13 or later")))
                }
                guard Bundle.main.bundleIdentifier != nil else { return reply(.failure(asukaNoLoginBundle)) }
                let s = SMAppService.mainApp
                let on = args.first == 1
                let status = s.status
                do {
                    if on && status == .enabled || !on && status != .enabled && status != .requiresApproval {
                        // already so
                    } else if on {
                        do { try s.register() } catch {
                            if s.status != .requiresApproval {
                                throw AsukaModuleError("asuka/login: the login item wasn't registered: " + asukaErrorText(error))
                            }
                        }
                    } else {
                        do { try s.unregister() } catch {
                            if s.status == .enabled {
                                throw AsukaModuleError("asuka/login: the login item wasn't removed: " + asukaErrorText(error))
                            }
                        }
                    }
                } catch {
                    return reply(.failure(error))
                }
                // Registered, but the user has to allow it in System Settings.
                if on && s.status == .requiresApproval { return reply(.success(Data([2]))) }
                reply(.success(Data()))
            },
            "enabled": { _, reply in
                // Not a login item where it can't be one.
                guard #available(macOS 13.0, *), Bundle.main.bundleIdentifier != nil else { return reply(.success(Data([0]))) }
                reply(.success(Data([SMAppService.mainApp.status == .enabled ? 1 : 0])))
            },
            "openedAtLogin": { _, reply in
                let f = AsukaFiles.shared
                if !f.launched {
                    f.loginWaiting.append(reply)
                    return
                }
                reply(.success(Data([f.atLogin ? 1 : 0])))
            },
        ]
    }
}

/// An app without a bundle (a plain executable) has no login item.
let asukaNoLoginBundle = AsukaModuleError("asuka/login: a login item needs the app's bundle: build it with asuka build, or run it with asuka dev")
