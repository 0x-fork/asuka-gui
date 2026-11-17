import XCTest
import AppKit
import Carbon.HIToolbox
@testable import AsukaHost

// The shell's pure parts: menus on the wire (ops/menu.go's format and its
// tests), shortcuts as key equivalents, the pseudo locales' menu words, and
// apps.search's ranking (the cases every desktop host answers alike).
@MainActor final class ShellTests: XCTestCase {
    // MARK: Menus

    /// A menu as ops.AppendMenu writes it.
    private struct Item {
        var flags: UInt8 = 0
        var title = ""
        var key = ""
        var mods: UInt8 = 0
        var role: UInt8 = 0
        var items: [Item] = []
    }

    private func append(_ w: inout AsukaByteWriter, _ items: [Item]) {
        w.u32(UInt32(items.count))
        for it in items {
            var flags = it.flags & ~AsukaMenuItem.submenu
            if !it.items.isEmpty { flags |= AsukaMenuItem.submenu }
            w.u8(flags)
            w.string(it.title)
            w.string(it.key)
            w.u8(it.mods)
            w.u8(it.role)
            if flags & AsukaMenuItem.submenu != 0 { append(&w, it.items) }
        }
    }

    private func menu(_ items: [Item]) -> [UInt8] {
        var w = AsukaByteWriter()
        append(&w, items)
        return [UInt8](w.data)
    }

    func testMenuRoundTrip() {
        let items = [
            Item(title: "Open", key: "o", mods: 1),
            Item(flags: AsukaMenuItem.separator),
            Item(title: "Export", items: [
                Item(title: "PDF", key: "e", mods: 3),
                Item(flags: AsukaMenuItem.disabled | AsukaMenuItem.checked, title: "Off"),
            ]),
            Item(flags: AsukaMenuItem.destructive, title: "Delete"),
            Item(title: "", role: AsukaMenuItem.roleQuit),
        ]
        let got = AsukaMenus.decode(menu(items))
        XCTAssertEqual(got.map(\.title), ["Open", "", "Export", "Delete", ""])
        XCTAssertEqual(got.map(\.index), [0, 1, 2, 5, 6], "submenus' items are numbered after the item that opens them")
        XCTAssertTrue(got[1].is(AsukaMenuItem.separator))
        XCTAssertTrue(got[2].is(AsukaMenuItem.submenu))
        XCTAssertEqual(got[2].items.map(\.title), ["PDF", "Off"])
        XCTAssertEqual(got[2].items.map(\.index), [3, 4])
        XCTAssertEqual(got[2].items[0].key, "e")
        XCTAssertEqual(got[2].items[0].mods, 3)
        XCTAssertTrue(got[2].items[1].is(AsukaMenuItem.disabled))
        XCTAssertTrue(got[2].items[1].is(AsukaMenuItem.checked))
        XCTAssertTrue(got[3].is(AsukaMenuItem.destructive))
        XCTAssertEqual(got[4].role, AsukaMenuItem.roleQuit)
        XCTAssertEqual(AsukaMenus.flatten(got).map(\.index), Array(0...6), "an item's index is its place in the flattened list")
    }

    func testMenuBarNumbersAcrossMenus() {
        var w = AsukaByteWriter()
        w.u32(2)
        w.string("File")
        append(&w, [Item(title: "New"), Item(title: "Open")])
        w.string("Tools")
        append(&w, [Item(title: "Run")])
        let bar = AsukaMenus.decodeBar([UInt8](w.data))
        XCTAssertEqual(bar.map(\.title), ["File", "Tools"])
        XCTAssertEqual(bar[1].items[0].index, 2)
    }

    func testMenuCutShort() {
        let b = menu([Item(title: "One"), Item(title: "Two"), Item(title: "Three")])
        // A short menu ends where it's cut.
        let got = AsukaMenus.decode(Array(b.dropLast(3)))
        XCTAssertEqual(got.map(\.title), ["One", "Two"])
        XCTAssertEqual(AsukaMenus.decode([]).count, 0)
        // A menu bar whose last menu has nothing left is cut before it.
        var w = AsukaByteWriter()
        w.u32(2)
        w.string("File")
        append(&w, [Item(title: "New")])
        w.string("Edit")
        XCTAssertEqual(AsukaMenus.decodeBar([UInt8](w.data)).map(\.title), ["File"])
    }

    func testMenuDepthBounded() {
        var it = Item(title: "leaf")
        for i in 0..<20 { it = Item(title: "level \(i)", items: [it]) }
        let got = AsukaMenus.flatten(AsukaMenus.decode(menu([it])))
        XCTAssertLessThanOrEqual(got.count, AsukaMenus.depth + 1, "submenus deeper than the bound are cut")
    }

    // MARK: Shortcuts

    func testKeyEquivalents() {
        func k(_ key: String, _ mods: UInt8) -> (String, NSEvent.ModifierFlags) { asukaKeyEquivalent(key, mods) }
        XCTAssertEqual(k("", 1).0, "")
        XCTAssertEqual(k("S", 1).0, "s", "a letter is lowercased: AppKit reads an uppercase one as shifted")
        XCTAssertEqual(k("s", 1).1, [.command])
        XCTAssertEqual(k("e", 3).1, [.command, .shift])
        XCTAssertEqual(k("x", 4 | 8).1, [.option, .control])
        XCTAssertEqual(k("enter", 1).0, "\r")
        XCTAssertEqual(k("Escape", 0).0, "\u{1b}")
        XCTAssertEqual(k("up", 1).0, String(Character(Unicode.Scalar(0xF700)!)))
        XCTAssertEqual(k("pagedown", 1).0, String(Character(Unicode.Scalar(0xF72D)!)))
        XCTAssertEqual(k("f1", 0).0, String(Character(Unicode.Scalar(0xF704)!)))
        XCTAssertEqual(k("f12", 0).0, String(Character(Unicode.Scalar(0xF70F)!)))
        XCTAssertEqual(k("f13", 0).0, "f13", "past F12: not a function key's equivalent")
    }

    func testHotkeyNamedKeys() {
        let r = AsukaHotkeys.carbonHotKey("Space", 1 | 4)
        XCTAssertEqual(r?.0, 0x31)
        XCTAssertEqual(r?.1, UInt32(cmdKey | optionKey))
        XCTAssertEqual(AsukaHotkeys.carbonHotKey("f20", 8)?.0, 0x5A)
        XCTAssertNil(AsukaHotkeys.carbonHotKey("nope", 1))
    }

    // MARK: Pseudo locales

    func testPseudoMenuWords() {
        XCTAssertNil(asukaPseudo("File", "de"))
        XCTAssertEqual(asukaPseudo("Edit", "en-XA"), "[Éðîţ one]")
        XCTAssertEqual(asukaPseudo("Bring All to Front", "en-xa"), "[Ɓŕîñĝ Åļļ ţö Ƒŕöñţ one two]")
        XCTAssertEqual(asukaPseudo("Close Window", "ar-XB"),
                       "\u{200F}\u{202E}Close\u{202C}\u{200F} \u{200F}\u{202E}Window\u{202C}\u{200F}")
    }

    // MARK: apps.search (the desktop hosts' cases)

    private func search(_ names: [String], _ q: String) -> [String] {
        AsukaAppSearch.rank(names, q).map { names[$0] }
    }

    func testSearch() {
        let installed = [
            "Activity Monitor", "App Store", "Calculator", "Calendar", "Café Mocha",
            "Code", "Fluß", "Google Chrome", "iPhone Mirroring", "Microsoft Word",
            "Office365", "Photo Booth", "Preview", "System Settings", "TextEdit",
            "Visual Studio Code", "VLC", "VSCodium", "Xcode", "XMLParser", "Ｗｉｄｅ",
        ]
        let cases: [(String, [String])] = [
            ("code", ["Code", "Visual Studio Code", "Xcode"]),
            ("cod", ["Code", "Visual Studio Code", "VSCodium", "Xcode", "Microsoft Word"]),
            ("cal", ["Calculator", "Calendar"]),
            ("CAL", ["Calculator", "Calendar"]),
            ("  cal  ", ["Calculator", "Calendar"]),
            ("calendar", ["Calendar"]),
            ("vsc", ["VSCodium", "Visual Studio Code"]),
            ("visstu", ["Visual Studio Code"]),
            ("vis stu", ["Visual Studio Code"]),
            ("studio code", ["Visual Studio Code"]),
            ("gc", ["Google Chrome"]),
            ("edit", ["TextEdit"]),
            ("te", ["TextEdit", "System Settings", "App Store", "Visual Studio Code"]),
            ("parser", ["XMLParser"]),
            ("xmlp", ["XMLParser"]),
            ("phone", ["iPhone Mirroring"]),
            ("365", ["Office365"]),
            ("mw", ["Microsoft Word"]),
            ("cafe", ["Café Mocha"]),
            ("CAFÉ", ["Café Mocha"]),
            ("cafè", ["Café Mocha"]),
            ("fluss", ["Fluß"]),
            ("wide", ["Ｗｉｄｅ"]),
            ("ode", ["Code", "Visual Studio Code", "Xcode"]),
            ("clc", ["Calculator"]),
            ("xcd", ["Xcode"]),
            ("monitor", ["Activity Monitor"]),
            ("store", ["App Store"]),
            ("zzz", []),
            ("qx", []),
        ]
        for (q, want) in cases {
            XCTAssertEqual(search(installed, q), want, "search \(q)")
        }
    }

    func testSearchRanks() {
        var list = ["Xcalc", "Mini Calc", "Calc", "Calculator", "Cxaxlxc", "Abacus Calc Pro", "CalcPad", "Notes"]
        let want = ["Calc", "CalcPad", "Calculator", "Abacus Calc Pro", "Mini Calc", "Xcalc", "Cxaxlxc"]
        XCTAssertEqual(search(list, "calc"), want)
        list.reverse()
        XCTAssertEqual(search(list, "calc"), want, "within a rank, by name whatever the order")
        let S = AsukaAppSearch.self
        let cases: [(String, String, Int)] = [
            ("Calc", "calc", S.exact), ("Calculator", "calc", S.prefix), ("Mini Calc", "calc", S.word),
            ("Visual Studio Code", "vsc", S.word), ("Visual Studio Code", "vc", S.word), ("TextEdit", "tedit", S.word),
            ("Xcalc", "calc", S.substring), ("Cxaxlxc", "calc", S.subsequence), ("Notes", "calc", -1), ("Calc", "calcs", -1),
        ]
        for (name, query, r) in cases {
            let q = S.prepare(query)
            XCTAssertEqual(S.rank(S.prepare(name), q.text, q.compact), r, "rank(\(name), \(query))")
        }
        XCTAssertEqual(S.fold("Ärger ÆØ straße ﬁle"), "arger aeo strasse file")
    }

    // MARK: Routes (the desktop hosts' file)

    private func location(_ href: String, _ state: String, _ key: String) -> [UInt8] {
        var w = AsukaByteWriter()
        w.u8(0) // navigate
        w.u32(0)
        w.string(href)
        w.data(Array(state.utf8))
        w.string(key)
        return w.bytes
    }

    func testRoutesKeptByWindow() throws {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("asuka-routes-" + UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: dir) }
        let path = dir.appendingPathComponent(AsukaRoutes.fileName).path
        let r = AsukaRoutes(path: path)
        r.location(location("/inbox/18", "{\"q\":1}", "main"))
        r.location(location("/stats", "", "statistics"))
        r.location(location("/x", "", "")) // a window without a key keeps nothing
        r.location([1, 2]) // cut short: nothing
        r.flush()
        let obj = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: path))) as? [String: [String: String]]
        XCTAssertEqual(obj, ["main": ["href": "/inbox/18", "state": "{\"q\":1}"], "statistics": ["href": "/stats"]],
                       "the state left out when it's empty, as routestate writes it")
        // The next run reads it back.
        let again = AsukaRoutes(path: path)
        XCTAssertEqual(again.entry("main"), AsukaRoutes.Entry(href: "/inbox/18", state: Array("{\"q\":1}".utf8)))
        XCTAssertEqual(again.entry("statistics")?.href, "/stats")
        XCTAssertNil(again.entry(""))
        // Nothing changed: nothing written.
        try FileManager.default.removeItem(atPath: path)
        again.location(location("/stats", "", "statistics"))
        again.flush()
        XCTAssertFalse(FileManager.default.fileExists(atPath: path))
    }

    func testRoutesKeptNowhere() {
        let r = AsukaRoutes(path: "")
        r.location(location("/b", "", "main"))
        r.flush()
        XCTAssertEqual(r.entry("main")?.href, "/b", "kept for the run, written nowhere")
    }
}
