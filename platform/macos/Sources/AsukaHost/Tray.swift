// platform/tray on a Mac (the "tray" module; sdk/tray has its protocol): each
// icon is an NSStatusItem in the menu bar, its button showing the image (a
// template: AppKit colors it as the menu bar is) and the title, its menu an
// NSMenu. Without on click the menu is the item's own, which AppKit opens on
// any click; with it, a click is the app's click and a secondary click (a
// right click, a Control click) opens the menu. The Dock menu's items are
// the application delegate's (applicationDockMenu), above the Dock's own.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

/// An icon in the menu bar.
@MainActor final class AsukaTrayIcon {
    let id: UInt32
    let item: NSStatusItem
    var menu: NSMenu?
    var flags: UInt8 = 0
    var image = "", symbol = "", title = "", tip = "", key = ""
    var items: [AsukaMenuItem] = []
    /// The image being loaded.
    var loading = ""
    /// Where the copy of the button the user last clicked shows on screen
    /// (AppKit's coordinates), while its window keeps the frame it had
    /// then (of): a flyout hangs from it.
    var clicked = CGRect.zero, of = CGRect.zero

    init(id: UInt32, item: NSStatusItem) {
        self.id = id
        self.item = item
    }
}

@MainActor final class AsukaTray: NSObject {
    static let shared = AsukaTray()
    var rt: AsukaRuntime { AsukaRuntime.shared }

    static let template: UInt8 = 1
    static let clickable: UInt8 = 2

    var icons: [UInt32: AsukaTrayIcon] = [:]
    var dock: [AsukaMenuItem] = []
    /// What happened that "next" hasn't answered with yet, and the "next"
    /// waiting.
    var queue: [Data] = []
    var waiting: ((Result<Data, Error>) -> Void)?

    func methods() -> [String: AsukaModuleMethod] {
        [
            "supported": { _, reply in reply(.success(Data([1]))) },
            "set": { [unowned self] args, reply in
                var r = AsukaByteReader(args)
                let id = r.u32(), flags = r.u8()
                var strs: [String] = []
                for _ in 0..<5 { strs.append(r.string()) }
                if r.bad { return reply(.failure(AsukaModuleError("tray.set: bad arguments"))) }
                self.set(id, flags, strs, AsukaMenus.decode(r.rest()))
                reply(.success(Data()))
            },
            "remove": { [unowned self] args, reply in
                var r = AsukaByteReader(args)
                let id = r.u32()
                if !r.bad { self.remove(id) }
                reply(.success(Data()))
            },
            "dock": { [unowned self] args, reply in
                self.dock = AsukaMenus.decode([UInt8](args))
                reply(.success(Data()))
            },
            "next": { [unowned self] _, reply in
                if !self.queue.isEmpty { return reply(.success(self.queue.removeFirst())) }
                self.waiting = reply
            },
            "stop": { [unowned self] _, reply in
                let w = self.waiting
                self.waiting = nil
                self.queue = []
                w?(.failure(AsukaModuleError("stopped")))
                reply(.success(Data()))
            },
        ]
    }

    /// Reports what happened to an icon (id 0: the Dock menu): kind 1 a
    /// click, 2 an item chosen; to the "next" waiting, or queued.
    func happened(_ id: UInt32, _ kind: UInt8, _ index: Int) {
        var w = AsukaByteWriter()
        w.u32(id)
        w.u8(kind)
        w.u32(UInt32(truncatingIfNeeded: index))
        if let reply = waiting {
            waiting = nil
            reply(.success(w.data))
            return
        }
        queue.append(w.data)
    }

    /// Makes or changes an icon: its image, title, tooltip and menu.
    func set(_ id: UInt32, _ flags: UInt8, _ strs: [String], _ items: [AsukaMenuItem]) {
        let t: AsukaTrayIcon
        var made = false
        if let x = icons[id] {
            t = x
        } else {
            made = true
            let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
            t = AsukaTrayIcon(id: id, item: item)
            icons[id] = t
            if !strs[4].isEmpty { item.autosaveName = "asuka " + strs[4] }
            if let b = item.button {
                b.target = self
                b.action = #selector(clicked(_:))
                // The left and right button's up, so the right one is ours too.
                b.sendAction(on: [.leftMouseUp, .rightMouseUp])
                // A changing number's digits keep their width.
                let size = NSFont.menuBarFont(ofSize: 0).pointSize
                b.font = NSFont.monospacedDigitSystemFont(ofSize: size, weight: .regular)
            }
        }
        let button = t.item.button
        // A new icon shows its image, its symbol, or the app's icon (with
        // neither, nothing "changed").
        let imageChanged = made || t.image != strs[0] || t.symbol != strs[1] || (t.flags ^ flags) & Self.template != 0
        t.flags = flags
        t.image = strs[0]
        t.symbol = strs[1]
        if t.title != strs[2] {
            t.title = strs[2]
            button?.title = t.title
            placeImage(t)
        }
        if t.tip != strs[3] {
            t.tip = strs[3]
            button?.toolTip = t.tip
        }
        t.key = strs[4]
        if imageChanged { loadImage(t) }
        t.items = items
        t.menu = items.isEmpty ? nil : statusMenu(items, #selector(trayMenuItem(_:)), Int(id) << 32)
        // Without on click, AppKit opens the item's menu on any click.
        t.item.menu = flags & Self.clickable == 0 ? t.menu : nil
    }

    /// A menu of items for a status item or the Dock: enabled as the app
    /// says (not by validation), without shortcuts.
    func statusMenu(_ items: [AsukaMenuItem], _ action: Selector, _ tagBase: Int) -> NSMenu {
        let m = NSMenu()
        m.autoenablesItems = false
        AsukaShell.shared.addMenuItems(m, items, action, tagBase, false)
        setMenuDirection(m, rt.rtl)
        return m
    }

    /// Shows the icon's image, loaded off the main thread, or its symbol,
    /// or, with neither, the app's icon.
    func loadImage(_ t: AsukaTrayIcon) {
        let button = t.item.button
        if t.image.isEmpty {
            t.loading = ""
            var img = rt.symbolImage(t.symbol)
            if img != nil {
                img?.isTemplate = true
            } else if t.symbol.isEmpty, let app = NSApp.applicationIconImage?.copy() as? NSImage {
                app.size = CGSize(width: 18, height: 18)
                img = app
            }
            button?.image = img
            placeImage(t)
            return
        }
        let src = t.image, id = t.id
        t.loading = src
        AsukaImageLoader.load(src, assets: rt.assets) { r in
            MainActor.assumeIsolated {
                let s = AsukaTray.shared
                guard let t = s.icons[id], t.loading == src else { return }
                t.loading = ""
                let button = t.item.button
                guard case .success(let c) = r, let img = c.image.copy() as? NSImage else {
                    // The symbol, if any, instead.
                    let img = s.rt.symbolImage(t.symbol)
                    img?.isTemplate = true
                    button?.image = img
                    s.placeImage(t)
                    return
                }
                // 16 points tall, its aspect kept.
                let sz = img.size
                if sz.height > 0 { img.size = CGSize(width: sz.width * 16 / sz.height, height: 16) }
                img.isTemplate = t.flags & Self.template != 0
                button?.image = img
                s.placeImage(t)
            }
        }
    }

    /// Puts the image before the title (after it, right to left), or alone.
    func placeImage(_ t: AsukaTrayIcon) {
        guard let b = t.item.button else { return }
        if t.title.isEmpty {
            b.imagePosition = .imageOnly
        } else if b.image == nil {
            b.imagePosition = .noImage
        } else {
            b.imagePosition = .imageLeading
        }
    }

    /// Takes an icon out of the menu bar.
    func remove(_ id: UInt32) {
        guard let t = icons.removeValue(forKey: id) else { return }
        NSStatusBar.system.removeStatusItem(t.item)
        t.menu = nil
    }

    /// A click on an icon's button: the app's click, or, a secondary
    /// click, or any without on click, its menu.
    @objc func clicked(_ button: NSStatusBarButton) {
        guard let t = icons.values.first(where: { $0.item.button === button }) else { return }
        let ev = NSApp.currentEvent
        noteClick(t, button, ev)
        var secondary = false
        if let ev {
            secondary = ev.type == .rightMouseDown || ev.type == .rightMouseUp || ev.modifierFlags.contains(.control)
        }
        if t.flags & Self.clickable != 0 && !secondary {
            happened(t.id, 1, 0)
            return
        }
        if t.menu != nil { openMenu(t) }
    }

    /// Keeps where on screen the copy of t's button the click ev was on
    /// shows (the menu bar of each display has one), for a flyout.
    func noteClick(_ t: AsukaTrayIcon, _ button: NSView, _ ev: NSEvent?) {
        guard let w = button.window, let ev, ev.window === w else { return }
        let of = w.frame
        let at = NSEvent.mouseLocation, inWin = ev.locationInWindow
        t.clicked = CGRect(x: at.x - inWin.x, y: at.y - inWin.y, width: of.width, height: of.height)
        t.of = of
    }

    /// Opens an icon's menu below it, its button highlighted, as AppKit
    /// opens a status item's own menu.
    func openMenu(_ t: AsukaTrayIcon) {
        t.item.menu = t.menu
        t.item.button?.performClick(nil)
        if t.flags & Self.clickable != 0 { t.item.menu = nil }
    }

    /// A tray menu's item chosen: its icon's number and the item's.
    @objc func trayMenuItem(_ mi: NSMenuItem) {
        let tag = UInt64(bitPattern: Int64(mi.tag))
        happened(UInt32(truncatingIfNeeded: tag >> 32), 2, Int(UInt32(truncatingIfNeeded: tag)))
    }

    /// The Dock menu's item chosen.
    @objc func dockMenuItem(_ mi: NSMenuItem) { happened(0, 2, mi.tag) }

    /// The Dock menu's items (applicationDockMenu), or none.
    func dockMenu() -> NSMenu? {
        dock.isEmpty ? nil : statusMenu(dock, #selector(dockMenuItem(_:)), 0)
    }

    // MARK: The automation

    var sortedIcons: [AsukaTrayIcon] { icons.keys.sorted().compactMap { icons[$0] } }

    /// Writes each tray icon's button as a PNG beside the window's snapshot
    /// (path.tray.png, path.tray2.png...), in the order the app made them.
    func snapshots(_ path: String) {
        let base = path.hasSuffix(".png") ? String(path.dropLast(4)) : path
        for (i, t) in sortedIcons.enumerated() {
            guard let b = t.item.button else { continue }
            AsukaAutomation.shared.writeViewPNG(b, base + (i == 0 ? ".tray.png" : ".tray\(i + 1).png"))
        }
    }

    /// What the icons show and their menus hold, and the Dock menu's
    /// (ASUKA_DUMP).
    func dump() -> String {
        var s = ""
        for t in sortedIcons {
            s += "tray \(t.id) title \(asukaQuote(t.title)) tooltip \(asukaQuote(t.tip))\n"
            dumpItems(&s, t.items, 1)
        }
        if !dock.isEmpty {
            s += "dock\n"
            dumpItems(&s, dock, 1)
        }
        return s
    }

    /// A menu's items, a line each, its submenus' indented under them.
    func dumpItems(_ s: inout String, _ items: [AsukaMenuItem], _ depth: Int) {
        for it in items {
            s += String(repeating: "  ", count: depth)
            if it.is(AsukaMenuItem.separator) {
                s += "---\n"
                continue
            }
            s += asukaQuote(it.title)
            if it.role == AsukaMenuItem.roleQuit { s += " quit" }
            if it.is(AsukaMenuItem.checked) { s += " checked" }
            if it.is(AsukaMenuItem.disabled) { s += " disabled" }
            s += "\n"
            dumpItems(&s, it.items, depth + 1)
        }
    }

    /// The tray icons and the Dock menu for the automation's JSON, and the
    /// Dock tile's progress.
    func json(_ d: inout [String: Any]) {
        let a = AsukaAutomation.shared
        d["tray"] = sortedIcons.map { t -> [String: Any] in
            var x: [String: Any] = ["id": t.id, "title": t.item.button?.title ?? "", "tooltip": t.item.button?.toolTip ?? "",
                                    "clickable": t.flags & Self.clickable != 0, "ownMenu": t.item.menu != nil]
            if let img = t.item.button?.image {
                x["image"] = true
                x["template"] = img.isTemplate
                x["imageSize"] = a.sizeJSON(img.size)
            }
            if let m = t.menu { x["menu"] = a.menuJSON(m) }
            if let p = t.item.button?.imagePosition { x["imagePosition"] = p.rawValue }
            return x
        }
        d["dockMenu"] = a.menuJSON(dockMenu())
        if let p = AsukaDock.json() { d["dockTile"] = p }
        d["hotkeys"] = AsukaHotkeys.shared.json()
    }

    /// Performs the item titled title in an icon's menu (any, or the icon
    /// numbered id) or the Dock's, as the user's choice: its target and
    /// action.
    func choose(_ title: String, dock: Bool) -> String? {
        let menus: [NSMenu] = dock ? [dockMenu()].compactMap { $0 } : sortedIcons.compactMap(\.menu)
        for m in menus {
            if let it = asukaFindItem(m, title) {
                guard let a = it.action else { return "\"\(title)\" does nothing" }
                if !it.isEnabled { return "\"\(title)\" is disabled" }
                NSApp.sendAction(a, to: it.target, from: it)
                return nil
            }
        }
        return "no item \"\(title)\""
    }
}

/// The item titled title in m or its submenus.
@MainActor func asukaFindItem(_ m: NSMenu, _ title: String) -> NSMenuItem? {
    for it in m.items {
        if it.title == title { return it }
        if let s = it.submenu, let x = asukaFindItem(s, title) { return x }
    }
    return nil
}

/// s quoted as Go's %q quotes it (the dump's format).
func asukaQuote(_ s: String) -> String {
    var out = "\""
    for u in s.unicodeScalars {
        switch u {
        case "\"": out += "\\\""
        case "\\": out += "\\\\"
        case "\n": out += "\\n"
        case "\t": out += "\\t"
        case "\r": out += "\\r"
        default:
            if u.value < 0x20 || u.value == 0x7F { out += String(format: "\\x%02x", u.value) } else { out.unicodeScalars.append(u) }
        }
    }
    return out + "\""
}

@MainActor extension AsukaShell {
    /// A tray icon's button (platform/tray's number for it): flyouts hang
    /// from it.
    func trayButton(id: UInt32) -> NSStatusBarButton? { AsukaTray.shared.icons[id]?.item.button }

    /// Where the copy of the tray icon's button the user last clicked
    /// shows (AppKit's screen coordinates), and the frame its window had
    /// then: a flyout hangs from it while the window is still there. nil
    /// if it wasn't clicked (opened from code).
    func trayClick(id: UInt32) -> (clicked: CGRect, of: CGRect)? {
        guard let t = AsukaTray.shared.icons[id], t.clicked.width > 0 else { return nil }
        return (t.clicked, t.of)
    }

    /// The automation's steps of the shell's: trayclick <id> (a click on
    /// the icon numbered id; secondary: its menu), traymenu <title>,
    /// dockmenu <title> (an item of a tray icon's menu, of the Dock's
    /// menu, chosen), reopen (the Dock icon clicked), hotkey <id> (the
    /// registered hot key pressed), openurls <a|b> (the app opened with files
    /// and links), poweroff (the session ending). nil: not a step of the
    /// shell's; "": done.
    func step(_ verb: String, _ arg: String) -> String? {
        let tray = AsukaTray.shared
        switch verb {
        case "trayclick":
            guard let id = UInt32(arg) ?? tray.sortedIcons.first?.id, let t = tray.icons[id], let b = t.item.button else { return "no tray icon \(arg)" }
            if t.flags & AsukaTray.clickable != 0 {
                tray.happened(t.id, 1, 0)
            } else {
                b.performClick(nil)
            }
            return ""
        case "traymenu":
            return tray.choose(arg, dock: false) ?? ""
        case "dockmenu":
            return tray.choose(arg, dock: true) ?? ""
        case "reopen":
            _ = AsukaAppDelegate.shared.applicationShouldHandleReopen(NSApp, hasVisibleWindows: NSApp.windows.contains { $0.isVisible && $0.canBecomeMain })
            return ""
        case "hotkey":
            guard let id = UInt32(arg) else { return "want hotkey <id>" }
            return AsukaHotkeys.shared.sendEvent(id) ?? ""
        case "openurls":
            // As Launch Services opens the app with them: paths and links,
            // separated by |.
            let urls = arg.split(separator: "|").map { s -> URL in
                s.hasPrefix("/") ? URL(fileURLWithPath: String(s)) : URL(string: String(s)) ?? URL(fileURLWithPath: String(s))
            }
            AsukaAppDelegate.shared.application(NSApp, open: urls)
            return ""
        case "poweroff":
            // The session ends (NSWorkspace.willPowerOffNotification), then
            // the quit it brings.
            willPowerOff(Notification(name: NSWorkspace.willPowerOffNotification))
            terminateLater()
            return ""
        default:
            return nil
        }
    }
}
