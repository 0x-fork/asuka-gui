import XCTest
@testable import AsukaHost

/// Asuka's own words come from the desktop hosts' shared C (words.c), in
/// the language the app shows, English where a catalog has none.
@MainActor final class WordsTests: XCTestCase {
    override func tearDown() { AsukaRuntime.shared.locale = "" }

    func testWordsInTheAppsLanguage() {
        let rt = AsukaRuntime.shared
        rt.locale = "en"
        XCTAssertEqual(asukaWord("about", ["app": "Notes"]), "About Notes")
        XCTAssertEqual(asukaBadged("Inbox", "3"), "Inbox, 3 items")
        XCTAssertEqual(asukaBadged("Inbox", "1"), "Inbox, 1 item")
        XCTAssertEqual(asukaBadged("Inbox", "new"), "Inbox, new")
        rt.locale = "de-DE"
        XCTAssertEqual(asukaWord("back"), "Zurück")
        XCTAssertEqual(asukaWord("about", ["app": "Notes"]), "Über „Notes“")
        rt.locale = "xx"
        XCTAssertEqual(asukaWord("cancel"), "Cancel")
    }
}
