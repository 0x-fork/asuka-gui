// The kitchen package's Swift: its module, on iOS and macOS.
import Asuka

final class SwiftKitchen: Kitchen {
    func name() throws -> String { "home" }

    func items(filter: String) throws -> [Item] {
        [Item(name: filter, weight: 1.5, unit: .gram, tags: ["dry", "fine"])]
    }
}

func makeKitchen() throws -> any Kitchen { SwiftKitchen() }
