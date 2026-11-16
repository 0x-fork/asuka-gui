// The packages' modules of part modules2, which the standard library's
// packages reach through asuka/host: contacts (Contacts.swift, asuka/contacts),
// calendar (Calendar.swift, asuka/calendar), photoLibrary and library
// (PhotoLibrary.swift, asuka/photos and asuka/library) and icloud
// (ICloud.swift's AsukaICloud, asuka/icloud).
//
// The rest (vision, scan, transcribe, nowPlaying, audioRecord, audio,
// speech, imageEdit, purchase, maps) isn't the host's: std's asuka/vision,
// asuka/scan, asuka/transcribe, asuka/nowplaying, asuka/record,
// asuka/audio, asuka/speech, asuka/imageedit, asuka/purchase and
// asuka/maps are native packages whose own Swift does them on the Mac, as
// on iOS.
//
// Here too: what the modules share (the caches' files, the alert that
// asks before adding a contact or an event), and the automation's seams,
// which replace what asks the user (a test can't answer a permission
// prompt, and must never meet one).
import AppKit

@MainActor extension AsukaRuntime {
    func registerModules2() {
        AsukaModules2Kit.appName = appName
        registerModule("contacts", AsukaContacts.shared.methods())
        registerModule("calendar", AsukaCalendar.shared.methods())
        registerModule("photoLibrary", AsukaPhotoLibrary.shared.methods())
        registerModule("library", AsukaLibrary.methods())
        registerModule("icloud", AsukaModules2Kit.icloudMethods())
    }

    /// The automation's steps of this part (Automation.swift's default):
    /// nil when verb isn't one of them, else the step's error (or none).
    ///   - access <name>=<status>[><after>]: what the system answers for
    ///     contacts, calendar, photos (read and write) or photosAdd (add
    ///     only), and what a request turns it into (unchanged without one)
    ///   - sheet <button>: presses the button of the alert over a window
    ///     (contacts.add's, calendar.add's)
    ///   - closepicker: closes the contact picker, as clicking outside it
    func modules2Step(_ verb: String, _ arg: String) -> String?? {
        switch verb {
        case "access":
            return .some(AsukaAccessFake.set(arg) ? nil : "want access <name>=<status>[><after>]")
        case "sheet":
            for w in NSApp.windows {
                guard let s = w.attachedSheet else { continue }
                if let b = asukaButton(in: s.contentView, titled: arg) {
                    b.performClick(nil)
                    return .some(nil)
                }
            }
            return .some("no sheet has a button \"\(arg)\"")
        case "closepicker":
            guard AsukaContacts.shared.pickerShown else { return .some("no contact picker shows") }
            AsukaContacts.shared.closePicker()
            return .some(nil)
        default:
            return nil
        }
    }

    /// What this part adds to the automation's JSON: the seams' asks (each
    /// request, calendar's as the access asked for), whether the contact
    /// picker is waited for and how many popovers show, and the last alert
    /// that asked before adding.
    func modules2JSON(_ d: inout [String: Any]) {
        var m: [String: Any] = ["contactsPicker": AsukaContacts.shared.pickerShown,
                                "popovers": NSApp.windows.filter { $0.isVisible && String(describing: type(of: $0)).contains("Popover") }.count]
        if AsukaAccessFake.on { m["asked"] = AsukaAccessFake.asked }
        if let a = AsukaModules2Kit.lastAlert { m["alert"] = ["title": a.title, "info": a.info] }
        d["modules2"] = m
    }
}

/// The first button of view's (or its subviews') titled title.
@MainActor func asukaButton(in view: NSView?, titled title: String) -> NSButton? {
    guard let view else { return nil }
    if let b = view as? NSButton, b.title == title { return b }
    for v in view.subviews {
        if let b = asukaButton(in: v, titled: title) { return b }
    }
    return nil
}

/// What the modules of this part share.
enum AsukaModules2Kit {
    /// The app's name, for its caches without a bundle (set as the modules
    /// are registered: workers read it).
    static var appName = ""

    /// A new file's path in the app's caches: Caches/<the app's ID>/folder,
    /// made, a UUID its name.
    static func cacheFile(_ folder: String, _ ext: String) throws -> String {
        let base = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory() + "/Library/Caches", isDirectory: true)
        var app = Bundle.main.bundleIdentifier ?? ""
        if app.isEmpty { app = appName }
        if app.isEmpty { app = "asuka" }
        let dir = base.appendingPathComponent(app, isDirectory: true).appendingPathComponent(folder, isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent(UUID().uuidString + ext).path
    }

    /// An error's description, or fallback without one.
    static func error(_ e: Error?, _ fallback: String) -> AsukaModuleError {
        guard let e else { return AsukaModuleError(fallback) }
        let s = (e as NSError).localizedDescription
        return AsukaModuleError(s.isEmpty ? fallback : s)
    }

    /// Asks with an alert over the window in use: its Add button (the
    /// default) or Cancel (Escape) calls done with whether it was Add.
    /// macOS has no new-contact screen nor event editor for apps (the
    /// iOS hosts' ContactsUI and EventKitUI screens).
    /// The last alert's message and informative text (the automation's
    /// JSON).
    @MainActor static var lastAlert: (title: String, info: String)?

    @MainActor static func confirm(_ title: String, _ info: String, _ add: String, _ done: @escaping (Bool) -> Void) {
        lastAlert = (title, info)
        let a = NSAlert()
        a.messageText = title
        a.informativeText = info
        a.addButton(withTitle: add)
        let cancel = a.addButton(withTitle: asukaWord("cancel"))
        cancel.keyEquivalent = "\u{1b}"
        a.beginSheetModal(for: AsukaRuntime.shared.keyWin.window) { resp in done(resp == .alertFirstButtonReturn) }
    }

    /// The JSON object of a module's arguments (nil: not an object).
    static func object(_ args: Data) -> [String: Any]? {
        (try? JSONSerialization.jsonObject(with: args)) as? [String: Any]
    }

    /// Runs work on a worker (what reads the system's stores), answering
    /// with what it gives on the main thread.
    static func offMain(_ reply: @escaping (Result<Data, Error>) -> Void, _ work: @escaping () throws -> Data) {
        DispatchQueue.global(qos: .userInitiated).async {
            let r = Result { try work() }
            DispatchQueue.main.async { reply(r) }
        }
    }

    // MARK: iCloud

    /// asuka/icloud's module: ICloud.swift's AsukaICloud (the iOS host's
    /// file), std/icloud's methods. An app's host always has it
    /// (the CLI's macHostDefines); without (the package's tests), there's
    /// no iCloud. Under the automation with ASUKA_ICLOUD_TEST_STORE, the
    /// key-value store is a UserDefaults suite of its own, emptied: the
    /// player has no entitlement for iCloud's.
    @MainActor static func icloudMethods() -> [String: AsukaModuleMethod] {
        #if ASUKA_ICLOUD
        if AsukaAutomation.shared.active && AsukaAutomation.shared.env["ASUKA_ICLOUD_TEST_STORE"] != nil {
            let d = UserDefaults(suiteName: AsukaICloud.testSuite)!
            d.removePersistentDomain(forName: AsukaICloud.testSuite)
            AsukaICloud.shared.kvStore = d
        }
        return Dictionary(uniqueKeysWithValues: AsukaICloud.methods.map { name in
            (name, { args, reply in AsukaICloud.shared.call(name, args, reply) } as AsukaModuleMethod)
        })
        #else
        let methods = ["account", "kvGet", "kvSet", "kvDelete", "kvAll", "kvSync", "container", "files", "watch", "unwatch",
                       "download", "evict", "read", "write", "delete", "move", "moveIn", "moveOut", "versions", "resolve", "link",
                       "next", "stop"]
        var m: [String: AsukaModuleMethod] = [:]
        for k in methods { m[k] = { _, reply in reply(.failure(AsukaModuleError("not available: this host has no iCloud"))) } }
        m["stop"] = { _, reply in reply(.success(Data())) }
        return m
        #endif
    }
}

/// The automation's seams for what asks the user: the access each store
/// answers, and what a request makes it, set by ASUKA_TEST_ACCESS
/// ("contacts=0>3,calendar=2") as the app starts or the access step.
/// Only under the automation (ASUKA_COMMANDS, ASUKA_SCRIPT): an app's
/// users never meet it.
@MainActor enum AsukaAccessFake {
    /// The statuses, as the system's raw values, by name; and the request's.
    private static var status: [String: Int] = [:]
    private static var after: [String: Int] = [:]
    /// The requests asked, by name (calendar's: "write" or "full" each).
    static var asked: [String: [String]] = [:]
    private static var loaded = false

    static var on: Bool {
        load()
        return !status.isEmpty
    }

    private static func load() {
        guard !loaded else { return }
        loaded = true
        guard AsukaAutomation.shared.active, let s = AsukaAutomation.shared.env["ASUKA_TEST_ACCESS"] else { return }
        for part in s.split(separator: ",") { _ = set(String(part)) }
    }

    /// Sets one: name=status, or name=status>after.
    static func set(_ spec: String) -> Bool {
        load()
        guard AsukaAutomation.shared.active else { return false }
        let kv = spec.split(separator: "=", maxSplits: 1).map { $0.trimmingCharacters(in: .whitespaces) }
        guard kv.count == 2 else { return false }
        let v = kv[1].split(separator: ">").map { Int($0) }
        guard let first = v.first, let s = first else { return false }
        status[kv[0]] = s
        if v.count > 1, let a = v[1] { after[kv[0]] = a } else { after[kv[0]] = nil }
        return true
    }

    /// The status the seam gives for name (nil: the system's).
    static func status(_ name: String) -> Int? {
        load()
        return status[name]
    }

    /// A request the seam answers (false: the system asks): it's counted,
    /// and the status becomes what it said.
    static func request(_ name: String, _ what: String = "") -> Bool {
        load()
        guard status[name] != nil else { return false }
        asked[name, default: []].append(what)
        if let a = after[name] { status[name] = a }
        return true
    }
}
