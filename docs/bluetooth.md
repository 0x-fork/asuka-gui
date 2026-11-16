# Bluetooth

`asuka/bluetooth` talks to Bluetooth Low Energy devices, as
react-native-ble-plx and Web Bluetooth do: as a central (find devices,
connect, read, write and follow their characteristics) and as a
peripheral (serve services, advertise). Core Bluetooth on iOS and macOS;
Android's BluetoothLeScanner and GATT; Web Bluetooth in Chromium.

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
`manufacturerData` (by company ID, four hex digits), `txPower` and
`connectable`. `duplicates` gives every advertisement, not only a
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
filters' services and `optionalServices`. Elsewhere it throws
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
  characteristics (once connected: it waits); `connection.characteristic
  (service, char)` finds one (`BluetoothError.notFound`).
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

## What the build adds

An app that imports the package is given what it needs:

| Platform | What |
| --- | --- |
| iOS | `NSBluetoothAlwaysUsageDescription` (say yours in `ios.usage`); the background modes of asuka.toml's `bluetooth.background` |
| Android | `BLUETOOTH_SCAN` (with `neverForLocation` unless `bluetooth.location`), `BLUETOOTH_CONNECT`, `BLUETOOTH_ADVERTISE`; `android.hardware.bluetooth_le`, not required |
| macOS | The purpose string, and the hardened runtime's `com.apple.security.device.bluetooth` entitlement |

## Each platform

| | iOS, macOS | Android | The web | Windows, Linux |
| --- | --- | --- | --- | --- |
| Scan | ✓ | ✓ | Behind Chromium's experimental flag (`requestLEScan`) | Not yet |
| Chooser | — | — | ✓ `requestDevice` | — |
| Connect, GATT | ✓ | ✓ | ✓ (the chooser's services) | Not yet |
| Serve, advertise | ✓ (name and services only) | ✓ | — | Not yet |
| MTU | The system's | Asked for (517) | 23 | |
| RSSI | ✓ | ✓ | — | |

On Windows and Linux, `state()` says unsupported for now and the rest
throw `notAvailable`: the desktop hosts' Go for them (WinRT's Bluetooth
LE, BlueZ) isn't ported to this package yet. Safari and Firefox have no
Web Bluetooth: unsupported.

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
`notified` and `lastRead()` say what the app did.

```asuka
test "a strap, heard" {
  let bt = bluetooth.fake()
  bt.add(bluetooth.FakePeripheral(id: "AA:BB:CC:00:00:01", name: "Polar H10", rssi: -58, advertised: ["180d"],
    services: [bluetooth.FakeService(uuid: "180d", characteristics: [bluetooth.FakeCharacteristic(uuid: "2a37", properties: [.notify])])]))
  expect(bluetooth.state() == .on)
}
```
