// STUB (part: shell). The app: its menu bar (the app's, File, Edit, View,
// Window, Help, with the app's MenuBar), the tray (NSStatusItem), the Dock
// (its menu, progress, attention), quitting (the Quit command,
// asuka_quit_request), files and links it's opened with, one instance and
// a second launch (SecondInstance), the background (an accessory app),
// lifecycle, routes (asuka-routes.json), crash records, the updater's
// module; and the shell's modules (dock, tray, hotkey, login, recent,
// apps, dialogs, files, clipboard, ...). Ports host/darwin's menubar.go,
// window.go's makeMenu, tray.go, dock.go, files.go, documents.go,
// dialogs.go, hotkey.go, apps.go, crash.go, routes.go, clipboard.go,
// system.go, system2.go, displays.go, power.go, appearance.go,
// services.go, periodic.go. The shell agent replaces this file (and may
// split it), keeping these functions' signatures.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

@MainActor final class AsukaShell {
    static let shared = AsukaShell()
    var rt: AsukaRuntime { AsukaRuntime.shared }

    /// Regular (a Dock icon and a menu bar), or an accessory in the
    /// background.
    var activationPolicy: NSApplication.ActivationPolicy { rt.config.background ? .accessory : .regular }
    /// The app runs in the background (asuka.toml's background).
    var inBackground: Bool { rt.config.background }
    /// Under asuka dev: this run starts once the one before has stopped.
    func waitForPreviousRun() {}
    /// Makes the main menu again (the app's menus, the language shown).
    func makeMenuBar() {}
    /// The MenuBar command.
    func setMenuBar(_ b: [UInt8]) {}
    func restoreState() {}
    /// Before the core starts: what must be in place (the notification
    /// center's delegate, quitting's handlers, the launch's Apple Events).
    func beforeCore() {}
    /// The Location command: a window's Router address, kept.
    func location(_ b: [UInt8]) {}
    func flushRoutes() {}
    /// The Quit command: the app quits as the user's Quit does.
    func quit() { NSApp.terminate(nil) }
    /// What the tray icons show, for the dump.
    var trayDump: String { "" }
    /// Writes each tray icon's button beside a snapshot (path.tray.png).
    func traySnapshots(_ path: String) {}
    /// What the shell adds to the automation's JSON (the tray, the Dock
    /// menu).
    func dumpJSON(_ d: inout [String: Any]) {}
}

/// Crash records: the core's (asuka_crash_start), and the Mac's crash
/// catching.
@MainActor enum AsukaCrash {
    static func start() {}
}

@MainActor extension AsukaRuntime {
    func registerShellModules() {}
}
