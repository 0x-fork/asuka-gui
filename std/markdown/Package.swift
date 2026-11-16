// swift-tools-version:5.9
// The std/markdown package's Swift, for iOS and macOS: the Asuka CLI builds it
// with the apps that import the package (asuka native).
import PackageDescription

let package = Package(
    name: "MarkdownNative",
    platforms: [.iOS("16.0"), .macOS("12.0")],
    products: [.library(name: "MarkdownNative", targets: ["MarkdownNative"])],
    dependencies: [
        .package(url: "https://github.com/egoist/asuka", from: "0.1.0"),
    ],
    targets: [
        .target(name: "MarkdownNative", dependencies: [.product(name: "Asuka", package: "asuka")], path: "swift"),
    ]
)
