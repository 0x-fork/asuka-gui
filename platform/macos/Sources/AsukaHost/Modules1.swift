// The packages' modules of part modules1: notifications (Notify.swift)
// and pushes (Push.swift), which asuka/notify and asuka/push reach through
// asuka/host.
//
// The camera, Bluetooth, location, Apple Pay and sign-in aren't here:
// std's asuka/camera, asuka/bluetooth, asuka/location, asuka/pay,
// asuka/appleauth and asuka/webauth are native packages whose own Swift
// does them on the Mac.
import AppKit

@MainActor extension AsukaRuntime {
    func registerModules1() {
        registerModule("notify", AsukaNotify.shared.methods())
        registerModule("push", AsukaPush.shared.methods())
    }

    /// What the automation's JSON shows of notify and push ("modules1"):
    /// whether the center's delegate is the host's and was installed before
    /// the app finished launching (to see a response that launched it), the
    /// Dock tile's badge, the categories kept, and what waits.
    func modules1JSON(_ d: inout [String: Any]) {
        d["modules1"] = ["notify": AsukaNotify.shared.json(), "push": AsukaPush.shared.json()]
    }
}
