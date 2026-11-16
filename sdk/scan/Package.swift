// swift-tools-version:5.9
// The platform/scan package's Swift, for iOS and macOS: the Asuka CLI builds it
// with the apps that import the package (asuka native).
import PackageDescription

let package = Package(
    name: "ScanNative",
    platforms: [.iOS("16.0"), .macOS("13.0")],
    products: [.library(name: "ScanNative", targets: ["ScanNative"])],
    dependencies: [
        .package(url: "https://github.com/egoist/asuka", from: "0.1.0"),
    ],
    targets: [
        .target(name: "ScanNative", dependencies: [.product(name: "Asuka", package: "asuka")], path: "swift"),
    ]
)
