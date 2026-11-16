# Bluetooth

`asuka/bluetooth` talks to Bluetooth Low Energy devices, as
react-native-ble-plx and Web Bluetooth do: as a central (find devices,
connect, read, write and follow their characteristics) and as a
peripheral (serve services, advertise). Core Bluetooth on iOS and macOS;
Android's BluetoothLeScanner and GATT; WinRT's Bluetooth LE on Windows;
BlueZ on Linux; Web Bluetooth in Chromium.

UUIDs are strings: a Bluetooth SIG 16-bit one as its four hex digits
(`"180d"`), any other as its 128 bits, lowercase. `bluetooth.uuid(u)`
writes one so, `bluetooth.fullUUID(u)` in its 128 bits, and the package
has the common ones: `heartRate`, `battery`, `deviceInformation`,
`heartRateMeasurement`, `batteryLevel`, `bodySensorLocation`,
`deviceName`, `manufacturerName`, ...

## The adapter and the permission

`bluetooth.state()` is tracked: `.on`, `.off`, `.unauthorized` (the
user hasn't allowed the app), `.unsupported`, `.resetting` or
`.unknown`; read in a binding, it follows the user turning Bluetooth off
or allowing the app. `bluetooth.request()` asks for the permission the
first time (iOS's prompt, Android 12's nearby devices), and returns the
state then.

```asuka
import asuka/bluetooth

component Adapter {
  let adapter = bluetooth.state()

  Column {
    Text("Bluetooth: {adapter}")
    Button("Allow") { on press { let _ = try? await bluetooth.request() } }
  }
}
```

## Finding devices

`bluetooth.scan(services:, duplicates:)` is a stream of what devices
advertise, as it's heard: each `Advertisement` has the `device` (its
`id` and `name`), its `rssi`, `localName`, `services`, `serviceData`,
`manufacturerData` (by company ID, four hex digits), `txPower`,
`connectable` and the `time` it was heard (a `time.Time`). `duplicates` gives every advertisement, not only a
device's first (to follow its RSSI); `services` only those of devices
advertising one of them. It scans while it's read, and throws
`BluetoothError.off`, `denied` or `notAvailable`.

```asuka
component Scanner {
  state found: [bluetooth.Advertisement] = []

  task {
    do {
      for await a in bluetooth.scan() {
        found.removeWhere(x => x.device.id == a.device.id)
        found.push(a)
      }
    } catch e {
      print(e.message)
    }
  }

  Column {
    for a in found key a.device.id {
      Text("{a.device.name} {a.rssi} dBm")
    }
  }
}
```

### The browser's chooser

A page can't scan (but behind a Chromium flag): `bluetooth.choose(filters,
optionalServices:)` shows the browser's chooser of devices that match one
of the `Filter`s (`services`, `name`, `namePrefix`), and returns the one
the user picked (`BluetoothError.canceled` if none). The page may use the
filters' services and `optionalServices`. On Windows it's the system's
device picker (which filters by name only). Elsewhere it throws
`notAvailable`: scan instead.

## Connecting

`bluetooth.connect(device, timeout:, autoConnect:)` returns a
`Connection`, a store: it connects as it's made, and keeps the
connection while its owner lives (the component that made it), or until
`close()`. Its `state` (`.connecting`, `.connected`, `.disconnecting`,
`.disconnected`) and `error` (why it disconnected) are state, which a
screen shows as they change.

```asuka
fn heartRate(v: bytes) -> int? {
  let flags = v.get(0) ?? 0
  if v.count() >= 3 && flags % 2 == 1 { return v.get(1)! + v.get(2)! * 256 }
  if v.count() >= 2 { return v.get(1)! }
  return none
}

component HeartRate(device: bluetooth.Device) {
  state bpm = ""
  let connection = bluetooth.connect(device, timeout: 20s)

  task {
    if let ch = try? await connection.characteristic(bluetooth.heartRate, bluetooth.heartRateMeasurement) {
      do {
        for await v in ch.notifications() {
          if let n = heartRate(v) { bpm = "{n} bpm" }
        }
      } catch e {
        bpm = e.message
      }
    }
  }

  Column {
    Text("{connection.state}")
    Text(bpm)
  }
}
```

- `connection.services()` is the device's services and their
  characteristics (once connected: it waits);
  `connection.characteristic(service, char)` finds one
  (`BluetoothError.notFound`).
- A `Characteristic` has its `uuid`, `service`, `properties` (`.read`,
  `.write`, `.writeWithoutResponse`, `.notify`, `.indicate`, ...) and
  `descriptors`. `read()` reads its value; `write(value)` writes it and
  returns once the device took it; `writeWithoutResponse(value)` doesn't
  wait (at most the MTU less 3); `notifications()` is a stream of its
  values, its notifications (or indications) on while it's read.
- `connection.mtu()` and `connection.rssi()`.

A disconnected connection's calls throw `BluetoothError.disconnected`.

## Being a peripheral

`bluetooth.serve(services)` serves `LocalService`s to the centrals that
connect, until its task is cancelled; `bluetooth.advertise(localName:,
services:, …)` advertises until its task is cancelled. A
`LocalCharacteristic` answers reads with its fixed `value`, or its
`onRead` (the central's ID: the value now); hears writes with `onWrite`;
`notify(value)` notifies the centrals subscribed, and `subscribers()`
(tracked) is who they are.

```asuka
component Thermometer {
  let level = bluetooth.LocalCharacteristic(uuid: bluetooth.batteryLevel, properties: [.read, .notify], permissions: [.readable],
    onRead: _ => bytes([87]))

  task {
    await all(serving([bluetooth.LocalService(uuid: bluetooth.battery, characteristics: [level])]), advertising())
  }

  Column {
    Text("{level.subscribers().count()} subscribed")
    Button("Notify") { on press { level.notify(bytes([86])) } }
  }
}

async fn serving(s: [bluetooth.LocalService]) { try? await bluetooth.serve(s) }
async fn advertising() { try? await bluetooth.advertise(localName: "Thermo", services: [bluetooth.battery]) }
```

## In the background

With the app out of sight its connections go on, and a scan can too:

- `bluetooth.backgroundScan(services, duplicates:)` scans for devices
  advertising one of the services, with the app in the background too.
  iOS scans so only for services, slower, and hears each device once; an
  app that calls it gets iOS's `bluetooth-central` mode. Android's is a
  scan the system runs, which goes on after the app's process has gone
  (Android pauses one without services while the screen is off). It
  stops as its reader does. The desktops scan only while the app runs;
  the web throws `notAvailable`, as `scan` does.
- With a background mode (asuka.toml's `bluetooth.background`, or
  `backgroundScan`'s), iOS keeps the app's connections and scan when it
  ends the app for memory, and starts the app again, without a screen,
  when one has news: a device notifies, the scan hears one. Android's
  background scan keeps what it hears while the app's process is gone.
- `bluetooth.restored()` is a stream of what came back, a `Restored`:
  `devices`, the connections iOS kept (connect to them again, which is
  at once for one connected); `scanServices`, the services of the scan
  iOS kept (scan for them again with `backgroundScan`, or stop it);
  `advertisements`, what a background scan heard with no scan of the
  app's reading it. What the launch brought comes first, kept until it's
  read: read it as the app starts, in a task of the app's root.
- `bluetooth.stopBackgroundScan()` stops the scan a previous run left
  (Android's, after its process went; iOS's, restored), once the app has
  what it heard. This run's background scans go on, and one of them
  replaces it too.

```asuka
state heard: [string] = []

async fn followRestored() throws {
  for await r in bluetooth.restored() {
    for a in r.advertisements { heard.push(a.device.id) }
    if !r.scanServices.isEmpty() { bluetooth.stopBackgroundScan() }
  }
}

async fn scanningAway() throws {
  for await a in bluetooth.backgroundScan([bluetooth.heartRate]) {
    heard.push(a.device.id)
  }
}

component Straps {
  task { try? await followRestored() }
  task { try? await scanningAway() }

  Column {
    for id in heard key id { Text(id) }
  }
}
```

iOS doesn't start the app again once the user has swiped it away in the
app switcher, and Android's scan stops with a reboot or as the user
force-stops the app.

## What the build adds

An app that imports the package is given what it needs:

| Platform | What |
| --- | --- |
| iOS | `NSBluetoothAlwaysUsageDescription` (say yours in `ios.usage`); the background modes of asuka.toml's `bluetooth.background`, and `bluetooth-central` for an app that calls `backgroundScan`; with a mode, the package's launch class, which makes the managers iOS restores before launch ends |
| Android | `BLUETOOTH_SCAN` (with `neverForLocation` unless `bluetooth.location`), `BLUETOOTH_CONNECT`, `BLUETOOTH_ADVERTISE`; `android.hardware.bluetooth_le`, not required; the background scan's receiver, for an app that calls `backgroundScan` |
| macOS | The purpose string, and the hardened runtime's `com.apple.security.device.bluetooth` entitlement |
| Linux | The Flatpak's `--system-talk-name=org.bluez` |

## Each platform

| | iOS, macOS | Android | Windows | Linux | The web |
| --- | --- | --- | --- | --- | --- |
| Scan | ✓ | ✓ | ✓ (an active scan: the names come) | ✓ (BlueZ's discovery) | Behind Chromium's experimental flag (`requestLEScan`) |
| Chooser | — | — | ✓ The system's device picker | — | ✓ `requestDevice` |
| Connect, GATT | ✓ | ✓ | ✓ | ✓ | ✓ (the chooser's services) |
| Serve | ✓ | ✓ | ✓ | ✓ | — |
| Advertise | The name and services only | ✓ | Services and manufacturer data, no name | ✓ | — |
| MTU | The system's | Asked for (517) | The system's | BlueZ's | 23 |
| RSSI | ✓ | ✓ | — | While BlueZ hears the device | — |
| Background scan | iOS: for services, slower; macOS: while the app runs | ✓ The system's, after the process | While the app runs | While the app runs | — |
| `restored()` | iOS: connections, the scan and what it heard | What the background scan heard | — | — | — |

On Windows, `request()` asks for the radio's access (an unpackaged app
has it); Linux asks no permission, and `request()` is `state()`. No
bluetoothd or no adapter is `unsupported`; a bus that won't let the app
talk to BlueZ is `unauthorized`. Windows doesn't say why a device went:
a lost connection's error says only that it was lost.
Safari and Firefox have no Web Bluetooth: unsupported.

## Testing

`bluetooth.fake()` gives the package a `FakeBluetooth`: an adapter
(`setState`), peripherals a test adds (`add(FakePeripheral(id:, name:,
rssi:, advertised:, services: [FakeService(uuid:, characteristics:
[FakeCharacteristic(uuid:, properties:, value:)])]))`), which scans
hear and connections take; `notify(device, service, char, value)` sends
a notification, `subscribed(…)` says whether the app follows one,
`remove(id)` takes a device away (its connections are lost), `written`
lists what the app wrote, `scans` and `connected` what runs. As a
peripheral, `centralRead`, `centralWrite` and `centralSubscribe` play a
central against the app's services; `servers`, `advertising`,
`notified` and `lastRead()` say what the app did. `restore(Restored(…))`
plays the system starting the app for Bluetooth (`restored()` gives it,
kept for its first reader); `backgroundScans` counts the background
scans running, and `backgroundLeft` says a previous run's is left
(`restore` leaves one when it brings back a scan or what one heard; a
background scan of the app's or `stopBackgroundScan()` ends it).

```asuka
test "a strap, heard" {
  let bt = bluetooth.fake()
  bt.add(bluetooth.FakePeripheral(id: "AA:BB:CC:00:00:01", name: "Polar H10", rssi: -58, advertised: ["180d"],
    services: [bluetooth.FakeService(uuid: "180d", characteristics: [bluetooth.FakeCharacteristic(uuid: "2a37", properties: [.notify])])]))
  expect(bluetooth.state() == .on)
}
```
