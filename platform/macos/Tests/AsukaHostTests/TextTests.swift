import AppKit
import XCTest
@testable import AsukaHost

// The list and text parts' pure logic: key names (keys.go's keyOf), the hour
// cycle of a pattern (locale.go's hourCycle), MIME patterns (ops.MatchMIME),
// styled text's encoding (ops/rich.go) and a rich field's formats
// (richfield.go), the lists' search (sort.Search).
final class TextTests: XCTestCase {
    private func key(_ code: UInt16, _ chars: String, _ flags: NSEvent.ModifierFlags) -> NSEvent {
        NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: flags, timestamp: 0, windowNumber: 0, context: nil,
                         characters: chars, charactersIgnoringModifiers: chars, isARepeat: false, keyCode: code)!
    }

    /// Named keys by their codes, characters as they type without
    /// modifiers (⇧ is a modifier), lower case (TestKeyOfDarwin).
    func testKeyOf() {
        let cases: [(UInt16, String, NSEvent.ModifierFlags, String, UInt8)] = [
            (0x24, "\r", [], "enter", 0),
            (0x4c, "\u{03}", [], "enter", 0),
            (0x33, "\u{7f}", [], "backspace", 0),
            (0x75, "\u{f728}", [], "delete", 0),
            (0x30, "\u{19}", .shift, "tab", 2),
            (0x7a, "\u{f704}", [], "f1", 0),
            (0x02, "D", [.command, .shift], "d", 3),
            (0x00, "a", [.control, .option], "a", 12),
        ]
        for (code, chars, flags, name, mods) in cases {
            let (n, m) = asukaKeyOf(key(code, chars, flags))
            XCTAssertEqual(n, name, "key \(code) \(chars.debugDescription)")
            XCTAssertEqual(m, mods, "key \(code) \(chars.debugDescription)")
        }
    }

    /// A pattern's hour cycle: quoted text isn't a field (TestEnvironmentLanguages).
    func testHourCycle() {
        XCTAssertEqual(asukaHourCycle("HH 'h'"), 24)
        XCTAssertEqual(asukaHourCycle("h a"), 12)
        XCTAssertEqual(asukaHourCycle("K:mm a"), 12)
        XCTAssertEqual(asukaHourCycle("k:mm"), 24)
        XCTAssertEqual(asukaHourCycle("'o''clock'"), 0)
    }

    func testMatchMIME() {
        XCTAssertTrue(asukaMatchMIME(["image/*"], "image/png"))
        XCTAssertTrue(asukaMatchMIME(["IMAGE/*"], "image/PNG; q=1"))
        XCTAssertFalse(asukaMatchMIME(["image/*"], "image/"))
        XCTAssertFalse(asukaMatchMIME(["image/*"], "text/plain"))
        XCTAssertTrue(asukaMatchMIME(["*/*"], "application/pdf"))
        XCTAssertTrue(asukaMatchMIME(["*"], "application/pdf"))
        XCTAssertTrue(asukaMatchMIME(["text/plain", "application/pdf"], "application/pdf"))
        XCTAssertFalse(asukaMatchMIME([], "text/plain"))
    }

    /// ops.AppendRich's encoding: styles, then paragraphs of runs.
    private func rich(_ styles: [AsukaRichStyle], _ paras: [[(Int, String)]]) -> [UInt8] {
        var w = AsukaByteWriter()
        w.u32(UInt32(styles.count))
        for s in styles {
            w.u8(UInt8(s.textStyle))
            w.f32(s.size)
            w.u8(UInt8(s.weight))
            w.u8(s.flags)
            w.u64(s.color)
            w.u64(s.background)
            w.string(s.link)
        }
        w.u32(UInt32(paras.count))
        for p in paras {
            w.u32(UInt32(p.count))
            for (si, t) in p {
                w.u32(UInt32(si))
                w.string(t)
            }
        }
        return w.bytes
    }

    func testRichContent() {
        let plain = AsukaRichStyle()
        var link = AsukaRichStyle()
        link.link = "https://example.com"
        link.flags = 4
        var bold = AsukaRichStyle()
        bold.weight = 7
        bold.size = 17
        let b = rich([plain, link, bold], [[(0, "Hello "), (1, "link")], [(2, "bye")]])
        let c = AsukaRichContent(b)
        XCTAssertEqual(c.styles, [plain, link, bold])
        XCTAssertEqual(c.paragraphs.count, 2)
        XCTAssertEqual(c.paragraphs[0].map { $0.text }, ["Hello ", "link"])
        XCTAssertEqual(c.paragraphs[0].map { $0.style }, [0, 1])
        XCTAssertEqual(c.paragraphs[1].map { $0.text }, ["bye"])
        XCTAssertTrue(c.styles[1].underline)
        // Cut short: what's whole before the cut.
        let cut = AsukaRichContent(Array(b.prefix(b.count - 2)))
        XCTAssertEqual(cut.styles.count, 3)
        XCTAssertEqual(cut.paragraphs.last?.count ?? 0, 0)
        XCTAssertEqual(AsukaRichContent([]).styles.count, 0)
    }

    /// TextFormat bits from and to a run's style (richfield.go's format and
    /// formatted).
    func testFormats() {
        var s = AsukaRichStyle()
        XCTAssertEqual(s.format, 0)
        s = s.formatted(1 | 2, true)
        XCTAssertEqual(s.weight, 7)
        XCTAssertTrue(s.italic)
        XCTAssertEqual(s.format, 3)
        s = s.formatted(16 | 4 | 8, true)
        XCTAssertEqual(s.format, 31)
        s = s.formatted(1, false)
        XCTAssertEqual(s.weight, 0)
        XCTAssertEqual(s.format, 30)
        var semibold = AsukaRichStyle()
        semibold.weight = 6
        XCTAssertEqual(semibold.format, 1) // semibold is bold
        XCTAssertEqual(semibold.formatted(1, true).weight, 7)
        XCTAssertEqual(semibold.formatted(2, false), semibold)
    }

    func testSearch() {
        let tops: [CGFloat] = [0, 40, 80, 120, 160]
        XCTAssertEqual(asukaSearch(4) { tops[$0 + 1] > 50 }, 1)
        XCTAssertEqual(asukaSearch(4) { tops[$0 + 1] > 0 }, 0)
        XCTAssertEqual(asukaSearch(4) { tops[$0 + 1] > 500 }, 4)
        XCTAssertEqual(asukaSearch(0) { _ in true }, 0)
    }
}
