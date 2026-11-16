// STUB (part: modules1). The packages' modules: camera (and its view),
// Bluetooth, location (and regions), notifications, push, Apple Pay,
// sign-in (Apple's, a web authentication session). Ports host/darwin's
// camera.go, bluetooth*.go, location*.go, notify.go, push.go, pay.go,
// auth.go, from the iOS host's Swift where it has them. The modules1 agent
// replaces this file (and may split it).
import AppKit

@MainActor extension AsukaRuntime {
    func registerModules1() {}
}
