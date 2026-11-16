// Action menus: a view's or a button's Menu prop (a button's pop-up on a
// tap, a view's context menu on a long press or a secondary click), a bar
// button's children. Their items send Action with their number: disabled
// ones dimmed, checked ones with a checkmark, submenus, sections between
// separators under a section header (macOS 14's) where they have a title,
// destructive ones' titles in the system's red, symbols beside them.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

@MainActor extension AsukaRuntime {
    /// Makes the view's menu: a button's pop-up (Tap), a view's context
    /// menu (LongPress: a secondary click shows it).
    func setMenu(_ n: AsukaNode) {
        if let m = n.menu {
            byMenu.removeValue(forKey: ObjectIdentifier(m))
            n.menu = nil
        }
        guard !n.menuActions.isEmpty, n.view != nil else { return }
        let m = makeMenuOf(n.menuActions)
        setMenuDirection(m, isRTL(n))
        n.menu = m
        byMenu[ObjectIdentifier(m)] = n
        // A view's Tap menu opens as a press is released on it (mouseUp).
    }

    /// Makes an NSMenu of actions whose items send menuItem: to the
    /// navigation's target, tagged with their number.
    func makeMenuOf(_ actions: [AsukaActionItem]) -> NSMenu {
        let m = NSMenu(title: "")
        m.autoenablesItems = false
        m.userInterfaceLayoutDirection = rtl ? .rightToLeft : .leftToRight
        addActionItems(m, actions)
        trimSeparators(m)
        return m
    }

    func addActionItems(_ m: NSMenu, _ actions: [AsukaActionItem]) {
        for a in actions where a.role != AsukaAlertButtonRole.cancel.rawValue {
            if a.inline {
                // A section: set off by separators (trimSeparators drops the
                // ones at the menu's ends and doubled ones).
                m.addItem(.separator())
                if !a.title.isEmpty {
                    if #available(macOS 14.0, *) {
                        m.addItem(NSMenuItem.sectionHeader(title: a.title))
                    } else {
                        let it = m.addItem(withTitle: a.title, action: nil, keyEquivalent: "")
                        it.isEnabled = false
                    }
                }
                addActionItems(m, a.items)
                m.addItem(.separator())
                continue
            }
            let it: NSMenuItem
            if a.submenu {
                it = m.addItem(withTitle: a.title, action: nil, keyEquivalent: "")
                let s = NSMenu(title: a.title)
                s.autoenablesItems = false
                s.userInterfaceLayoutDirection = rtl ? .rightToLeft : .leftToRight
                addActionItems(s, a.items)
                trimSeparators(s)
                it.submenu = s
            } else {
                it = m.addItem(withTitle: a.title, action: #selector(AsukaNavTarget.menuItem(_:)), keyEquivalent: "")
                it.target = navTarget
                it.tag = Int(a.index)
                if a.checked { it.state = .on }
            }
            it.isEnabled = !a.disabled
            if let img = symbolImage(a.icon) { it.image = img }
            if a.role == AsukaAlertButtonRole.destructive.rawValue { asukaDestructiveTitle(it, a.title) }
        }
    }

    /// Removes a menu's separators at its ends and those that follow
    /// another.
    func trimSeparators(_ m: NSMenu) {
        var prevSep = true // the start
        var i = 0
        while i < m.numberOfItems {
            let it = m.item(at: i)!
            if it.isSeparatorItem {
                if prevSep {
                    m.removeItem(at: i)
                    continue
                }
                prevSep = true
            } else {
                prevSep = false
            }
            i += 1
        }
        if m.numberOfItems > 0, m.item(at: m.numberOfItems - 1)!.isSeparatorItem { m.removeItem(at: m.numberOfItems - 1) }
    }

    /// A button's Tap menu, below it.
    func popUp(_ n: AsukaNode) {
        guard let m = n.menu, let v = n.view else { return }
        let at = CGPoint(x: 0, y: v.isFlipped ? v.bounds.height + 4 : -4)
        m.popUp(positioning: nil, at: at, in: v)
    }

    /// A menu's item was chosen: its node (the one whose menu it's in, or
    /// further up) hears its number.
    func menuItem(_ it: NSMenuItem) {
        // A submenu's item: its menu is the one that opens it, or further up.
        var m = it.menu
        var n: AsukaNode?
        while let x = m, n == nil {
            n = byMenu[ObjectIdentifier(x)]
            m = x.supermenu
        }
        guard let n else { return }
        let tag = Int64(it.tag)
        emit(n, .action) { AsukaEvents.action($0, tag) }
    }

    /// Chooses the item titled title of a menu a node or a toolbar item
    /// shows (a Menu prop's, a bar button's), in its submenus too, as a
    /// click on it would. The automation's "pick".
    func pickMenuItem(_ title: String) -> String? {
        func find(_ m: NSMenu) -> NSMenuItem? {
            for it in m.items {
                if let s = it.submenu, let x = find(s) { return x }
                if it.title == title && it.submenu == nil && !it.isSeparatorItem { return it }
            }
            return nil
        }
        var menus: [NSMenu] = []
        for n in nodes.values.sorted(by: { $0.id > $1.id }) {
            if let m = n.menu, let v = n.view, v.window?.isVisible == true, !v.isHiddenOrHasHiddenAncestor { menus.append(m) }
        }
        for w in allWins where w.window.isVisible { menus += w.chrome.menus }
        for m in menus {
            guard let it = find(m) else { continue }
            if !it.isEnabled { return "\"\(title)\" is disabled" }
            if it.action == #selector(AsukaNavTarget.menuItem(_:)) { menuItem(it) } else if let a = it.action { NSApp.sendAction(a, to: it.target, from: it) }
            return nil
        }
        return "no menu shows \"\(title)\""
    }
}

extension AsukaNavTarget {
    @objc func menuItem(_ sender: Any?) {
        guard let it = sender as? NSMenuItem else { return }
        rt.menuItem(it)
    }
}

/// Shows a menu item's title in the system's red, as macOS shows a
/// destructive item.
@MainActor func asukaDestructiveTitle(_ it: NSMenuItem, _ title: String) {
    it.attributedTitle = NSAttributedString(string: title, attributes: [.foregroundColor: NSColor.systemRed])
}
