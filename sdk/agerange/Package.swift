// swift-tools-version:5.9
// The platform/agerange package's Swift, for iOS and macOS: the Asuka CLI builds it
// with the apps that import the package (asuka native).
import PackageDescription

let package = Package(
    name: "AgerangeNative",
    platforms: [.iOS("16.0"), .macOS("11.0")],
    products: [.library(name: "AgerangeNative", targets: ["AgerangeNative"])],
    dependencies: [
        .package(url: "https://github.com/egoist/asuka", from: "0.1.0"),
    ],
    targets: [
        .target(name: "AgerangeNative", dependencies: [.product(name: "Asuka", package: "asuka")], path: "swift"),
    ]
)
