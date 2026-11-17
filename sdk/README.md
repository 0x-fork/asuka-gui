# The platform SDK

Each directory here is a package apps import as `platform/<name>`: what
wraps a device (the camera, location, Bluetooth, NFC, the sensors), an
operating system's service (notifications, pushes, the share sheet, the
Dock and the tray, iCloud, the keychain) or a store (purchases, Apple
Pay and Google Pay, reviews). The language's own library, the same on
every platform, is the standard library's, `std/<name>` (std/).
docs/stdlib.md lists both.

The CLI carries this directory (package `sdk`, sdk.go embeds it), so an
installed CLI needs no checkout; the compiler resolves
`platform/<name>` to `sdk/<name>` (std.Package). A package's asuka.toml
names it by its import path (`name = "platform/camera"`), and its
native code is named after that path on every platform: the Kotlin's
package `platform.camera`, the Swift's class `AsukaModule_platform_camera`,
the web's and the desktop's registration `"platform/camera"`.

`platform/host` (host/) is the bridge the packages over a host's own
module call it through (`host.call`, `host.send`, `host.next`): it's for
the packages here, not for apps.

How to write a package, test it and document it is the standard
library's README (std/README.md), which covers both.
