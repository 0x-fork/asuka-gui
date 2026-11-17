import Contacts
import XCTest
@testable import AsukaHost

// What the contacts, calendar and library modules compute on their own:
// a contact as add builds it, encoded as iOS encodes one; the alerts'
// texts; all-day events' days on the wire and back; the thumbnails'
// names; the library's numbered names.
@MainActor final class Modules2Tests: XCTestCase {
    func testContactBuiltAndEncoded() throws {
        let o = AsukaContacts.Args(try XCTUnwrap(JSONSerialization.jsonObject(with: Data("""
            {"givenName":"Ada","familyName":"Lovelace","organization":"Analytical Engines","jobTitle":"Programmer",
             "phones":[{"label":"","value":"+44 20 7946 0000"}],"emails":[{"label":"work","value":"ada@example.com"}],"birthday":"1815-12-10"}
            """.utf8)) as? [String: Any]))
        let c = AsukaContacts.json(AsukaContacts.build(o))
        for (k, want) in ["givenName": "Ada", "familyName": "Lovelace", "organization": "Analytical Engines",
                          "jobTitle": "Programmer", "birthday": "1815-12-10", "note": ""] {
            XCTAssertEqual(c[k] as? String, want, k)
        }
        let name = c["name"] as? String ?? ""
        XCTAssertTrue(name.contains("Ada") && name.contains("Lovelace"), name)
        let phones = c["phones"] as? [[String: String]] ?? []
        XCTAssertEqual(phones.count, 1)
        XCTAssertEqual(phones.first?["value"], "+44 20 7946 0000")
        XCTAssertFalse(phones.first?["label"]?.isEmpty ?? true, "a mobile number, its label localized")
        XCTAssertEqual(c["emails"] as? [[String: String]], [["label": "work", "value": "ada@example.com"]])

        // Only a name: it's the given name; a birthday without a year.
        var g = AsukaContacts.Args()
        g.name = "Grace"
        g.birthday = "--12-09"
        let gc = AsukaContacts.json(AsukaContacts.build(g))
        XCTAssertEqual(gc["givenName"] as? String, "Grace")
        XCTAssertEqual(gc["birthday"] as? String, "--12-09")
    }

    func testContactAlertAndBirthdays() {
        var o = AsukaContacts.Args()
        o.givenName = "Ada"
        o.familyName = "Lovelace"
        o.organization = "Analytical Engines"
        o.phones = [("mobile", "123")]
        let (title, info) = AsukaContacts.alertText(o)
        XCTAssertEqual(title, "Ada Lovelace")
        XCTAssertEqual(info, "Analytical Engines\nmobile: 123")
        XCTAssertEqual(AsukaContacts.birthday(nil, 3, 4), "--03-04")
        XCTAssertEqual(AsukaContacts.birthday(NSDateComponentUndefined, 3, 4), "--03-04")
        XCTAssertEqual(AsukaContacts.birthday(2000, nil, 4), "")
        XCTAssertEqual(AsukaContacts.birthday(2000, NSDateComponentUndefined, 4), "")
        XCTAssertNil(AsukaContacts.parseBirthday("12-09"))
        XCTAssertEqual(AsukaContacts.parseBirthday("--12-09")?.month, 12)
    }

    func testCalendarTimes() {
        let ny = TimeZone(identifier: "America/New_York")!
        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = ny
        var utc = Calendar(identifier: .gregorian)
        utc.timeZone = TimeZone(identifier: "UTC")!
        func at(_ c: Calendar, _ y: Int, _ mo: Int, _ d: Int, _ h: Int = 0, _ mi: Int = 0, _ s: Int = 0) -> Date {
            c.date(from: DateComponents(year: y, month: mo, day: d, hour: h, minute: mi, second: s))!
        }
        func ms(_ d: Date) -> Int64 { Int64(d.timeIntervalSince1970 * 1000) }

        // All-day events: their days at UTC midnight on the wire, the end
        // the day after the last; and back.
        let (s, e) = AsukaCalendar.eventMs(at(cal, 2024, 3, 10), at(cal, 2024, 3, 11, 23, 59, 59), allDay: true, ny)
        XCTAssertEqual(s, ms(at(utc, 2024, 3, 10)))
        XCTAssertEqual(e, ms(at(utc, 2024, 3, 12)))
        let a = at(cal, 2024, 3, 10, 9), b = at(cal, 2024, 3, 10, 10)
        let (ts, te) = AsukaCalendar.eventMs(a, b, allDay: false, ny)
        XCTAssertEqual(ts, ms(a))
        XCTAssertEqual(te, ms(b))

        var (st, en) = AsukaCalendar.addTimes(s, e, allDay: true, ny, now: Date())
        XCTAssertEqual(st, at(cal, 2024, 3, 10))
        XCTAssertEqual(en, at(cal, 2024, 3, 11, 23, 59, 59))
        (st, en) = AsukaCalendar.addTimes(ms(b), ms(a), allDay: false, ny, now: Date())
        XCTAssertEqual(st, b, "an end before the start")
        XCTAssertEqual(en, b.addingTimeInterval(3600), "an end before the start: an hour")
        (st, en) = AsukaCalendar.addTimes(0, 0, allDay: false, ny, now: at(cal, 2024, 3, 10, 9, 41))
        XCTAssertEqual(st, at(cal, 2024, 3, 10, 10), "no start: the next hour")
        XCTAssertEqual(en, st.addingTimeInterval(3600))
    }

    func testCalendarAlert() {
        var o = AsukaCalendar.Args()
        o.title = "Launch"
        o.location = "Cupertino"
        let (title, info) = AsukaCalendar.alertText(o, Date(timeIntervalSince1970: 1718035200), Date(timeIntervalSince1970: 1718038800))
        XCTAssertEqual(title, "Launch")
        XCTAssertTrue(info.hasSuffix("\nCupertino") && info.count > "\nCupertino".count, info)
        o.title = ""
        XCTAssertEqual(AsukaCalendar.alertText(o, Date(), Date()).title, "New Event")
        XCTAssertEqual(AsukaCalendar.hex(NSColor(srgbRed: 1, green: 0, blue: 0.5, alpha: 1)), "#ff007f")
    }

    func testPhotoThumbnailNames() {
        let a = AsukaPhotoLibrary.thumbName("id/L0/001", 1, 300), b = AsukaPhotoLibrary.thumbName("id/L0/001", 2, 300)
        XCTAssertNotEqual(a, b, "an edit makes another")
        XCTAssertTrue(a.hasSuffix("-300.jpg"), a)
    }

    func testLibraryNumbersTakenNames() throws {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString).path
        defer { try? FileManager.default.removeItem(atPath: dir) }
        let src = (dir as NSString).appendingPathComponent("src/report.txt")
        try FileManager.default.createDirectory(atPath: (src as NSString).deletingLastPathComponent, withIntermediateDirectories: true)
        try Data("hello".utf8).write(to: URL(fileURLWithPath: src))
        let out = (dir as NSString).appendingPathComponent("Downloads")
        XCTAssertEqual(try AsukaLibrary.saveFile(src, out, "Reports", "report.txt", move: false), out + "/Reports/report.txt")
        XCTAssertEqual(try AsukaLibrary.saveFile(src, out, "Reports", "report.txt", move: false), out + "/Reports/report (1).txt")
        XCTAssertEqual(try AsukaLibrary.saveFile(src, out, "", "noext", move: true), out + "/noext")
        XCTAssertFalse(FileManager.default.fileExists(atPath: src), "moved")
        XCTAssertEqual(try String(contentsOfFile: out + "/noext", encoding: .utf8), "hello")
    }
}
