// swift-tools-version:5.9
// The std/watch package's Swift, for macOS (FSEvents; phones have
// nothing to watch with): the Asuka CLI builds it with the apps that
// import the package (asuka native). Windows and Linux run its C
// (desktop/).
import PackageDescription

let package = Package(
    name: "WatchNative",
    platforms: [.macOS("11.0")],
    products: [.library(name: "WatchNative", targets: ["WatchNative"])],
    dependencies: [
        .package(url: "https://github.com/egoist/asuka", from: "0.1.0"),
    ],
    targets: [
        .target(name: "WatchNative", dependencies: [.product(name: "Asuka", package: "asuka")], path: "swift"),
    ]
)
