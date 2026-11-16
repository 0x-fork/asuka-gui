// swift-tools-version:5.9
// std/lottie for iOS and macOS: lottie-ios (its binary
// distribution, lottie-spm). The Asuka CLI builds it with the apps that
// import std/lottie.
import PackageDescription

let package = Package(
    name: "LottieNative",
    platforms: [.iOS("16.0"), .macOS("11.0")],
    products: [.library(name: "LottieNative", targets: ["LottieNative"])],
    dependencies: [
        .package(url: "https://github.com/egoist/asuka", from: "0.1.0"),
        .package(url: "https://github.com/airbnb/lottie-spm.git", from: "4.5.0"),
    ],
    targets: [
        .target(name: "LottieNative", dependencies: [
            .product(name: "Asuka", package: "asuka"),
            .product(name: "Lottie", package: "lottie-spm"),
        ], path: "swift"),
    ]
)
