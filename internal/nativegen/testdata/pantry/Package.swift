// swift-tools-version:5.9
// The pantry package's Swift, for iOS and macOS: the Asuka CLI builds it
// with the apps that import the package (asuka native).
import PackageDescription

let package = Package(
    name: "PantryNative",
    platforms: [.iOS("16.0"), .macOS("11.0")],
    products: [.library(name: "PantryNative", targets: ["PantryNative"])],
    dependencies: [
        .package(url: "https://github.com/egoist/asuka", from: "0.1.0"),
    ],
    targets: [
        .target(name: "PantryNative", dependencies: [.product(name: "Asuka", package: "asuka")], path: "swift"),
    ]
)
