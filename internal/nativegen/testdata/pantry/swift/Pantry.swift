// The pantry package's Swift: what pantry.asuka declares, implemented on
// iOS and macOS (built into a Mac app by cmd/asuka's TestMacSwift; the
// GUI tests' plugin app calls it).
import Asuka
import Foundation
#if canImport(UIKit)
import UIKit
#else
import AppKit
#endif

final class SwiftPantry: Pantry {
    func name() throws -> String { "home" }
    func items(filter: String) throws -> [Item] {
        [Item(name: filter, weight: 1.5, level: .high, tags: ["a", "b"], notes: ["k": "v"], next: Item(name: "inner"),
              data: Data([1, 2, 3]), took: 2.5, count: -3, fresh: true)]
    }
    func add(item: Item) throws { if item.name.isEmpty { throw PantryError.busy } }
    func cook(recipe: String, progress: @escaping (Double) -> Void) async throws -> Item {
        if recipe == "slow" {
            try await Task.sleep(nanoseconds: 10_000_000_000)
        }
        progress(0.5)
        progress(1)
        return Item(name: recipe + " done")
    }
    func ask(question: @escaping (String) -> Bool) throws -> Bool { question("ok?") }
    func `open`(name: String) throws -> any Shelf {
        if name == "none" { throw NSError(domain: "Shelf", code: 7, userInfo: [NSLocalizedDescriptionKey: "no shelf"]) }
        return try AsukaNative.onMain { SwiftShelf() }
    }
    func show() throws {}
    func counts() throws -> [String: Int] { ["a": 1, "b": -2] }
    func total(prices: [Decimal]) throws -> Decimal { prices.reduce(0, +) }
    func shows(view: AsukaViewRef) throws -> Bool { try AsukaNative.onMain { view.view != nil } }
}

@MainActor final class SwiftShelf: Shelf {
    func stack(count: Int, level: Level) throws {
        if level == .high && count > 10 { throw PantryError.empty }
    }
    func close() throws {}
}

func makePantry() throws -> any Pantry { SwiftPantry() }
@MainActor func makeShelf() throws -> any Shelf { SwiftShelf() }

#if canImport(UIKit)
@MainActor final class SwiftDial: Dial, AsukaDestroying {
    let slider = UISlider()
    let events: DialEvents
    init(_ events: DialEvents) { self.events = events }
    var view: AsukaPlatformView { slider }
    func setValue(_ value: Double) {
        slider.value = Float(value)
        events.changed(value: value * 2)
    }
    func setLabel(_ value: String) { slider.accessibilityLabel = value }
    func setItems(_ value: [Item]) {
        if let f = value.last { events.picked(item: f, index: value.count - 1) }
    }
    func destroy() {}
}
#else
@MainActor final class SwiftDial: Dial, AsukaSizing, AsukaDestroying {
    let slider = NSSlider()
    let events: DialEvents
    init(_ events: DialEvents) { self.events = events }
    var view: AsukaPlatformView { slider }
    func setValue(_ value: Double) {
        slider.doubleValue = value
        events.changed(value: value * 2)
    }
    func setLabel(_ value: String) { slider.toolTip = value }
    func setItems(_ value: [Item]) {
        if let f = value.last { events.picked(item: f, index: value.count - 1) }
    }
    func sizeThatFits(_ size: CGSize) -> CGSize { CGSize(width: 160, height: 24) }
    func destroy() {}
}
#endif

@MainActor func makeDial(_ events: DialEvents) -> any Dial { SwiftDial(events) }
