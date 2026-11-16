// swift-tools-version:5.9
// The asuka/host package's Swift, for iOS (macOS runs its Go: the Mac's
// host is Go): the Asuka CLI builds it with the apps that import the
// package (asuka native).
import PackageDescription

let package = Package(
    name: "HostNative",
    platforms: [.iOS("16.0")],
    products: [.library(name: "HostNative", targets: ["HostNative"])],
    dependencies: [
        .package(url: "https://github.com/egoist/asuka", from: "0.1.0"),
    ],
    targets: [
        .target(name: "HostNative", dependencies: [.product(name: "Asuka", package: "asuka")], path: "swift"),
    ]
)
