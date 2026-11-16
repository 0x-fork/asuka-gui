# The standard library

The standard library is the `asuka/...` packages. They come with the
`asuka` CLI, so they need no `asuka add`: import one and use it.

```asuka
import asuka/http
import asuka/store

state units = store.read("units", "metric")
```

Some are part of the compiler: they're plain functions with no platform
code. The others are [native packages](native.md) like any other: each
platform runs them with its own APIs (URLSession on iOS and macOS,
Android's own classes, the browser's on the web, Go on Windows and
Linux), so they behave as they do for the platform's own apps.

| Package | What it has |
|---|---|
| `asuka/json` | `json.decode<T>(text)` and `json.encode(value, indent:)`: typed JSON, by the compiler's knowledge of `T` |
| `asuka/math` | `sqrt`, `pow`, `floor`, `ceil`, `round`, `sin`, `cos`, `atan2`, `clamp`, `random`, `randomInt`, `seed`, `pi` |
| `asuka/time` | `time.now()` (nanoseconds since 1970), `time.monotonic()` (a `Duration` that only goes forward) |
| `asuka/http` | `http.get`, `http.post`, `http.postJSON`, `http.request`: requests through the platform's own client, so proxies, trusted certificates, caches and cookies work as the system's do. A `Response` has `status`, `url`, `headers`, `body`, `text()`, `json<T>()` and `ok()` |
| `asuka/network` | `network.status()` (tracked: `connected`, `type`, `metered`, `constrained`; it watches the network only while something reads it), `network.current()`, `network.changes()` (a stream). NWPathMonitor on iOS and macOS, ConnectivityManager on Android, the Network List Manager on Windows, GNetworkMonitor and NetworkManager on Linux, `navigator.onLine` and the Network Information API on the web |
| `asuka/store` | `store.read(key, default)`, `store.write(key, value)`, `store.remove(key)`: small values kept across launches as JSON, in the platform's preferences (UserDefaults, SharedPreferences, `localStorage`, a file on Windows and Linux) |
| `asuka/location` | `location.authorization()` (tracked), `request()`, `requestAlways()`, `requestPrecise(purpose)`, `current()`, `positions()` (a stream), `placeName(lat, lon)`, `openSettings()`, `supported()`; background updates (`startUpdates(…)`, `updates()`, `stopUpdates()`, `updating()`) and regions (`monitor(region)`, `crossings()`, `stopMonitoring(id)`, `monitored()`), their events kept until read and held while the app works (`hold()`). Core Location, Android's location manager, WinRT's Geolocator, the location portal or GeoClue, the Geolocation API. [Device and services](device.md#location-asukalocation) |
| `asuka/sensors` | `sensors.available(sensor)`; `accelerometer(every:)`, `gyroscope`, `magnetometer`, `motion` (attitude, gravity, user acceleration), `pressure`, `light`, `stepUpdates()` (streams); `steps(from:, to:)`; `simulate(json)`. Core Motion, SensorManager, the Generic Sensor API, iio-sensor-proxy. [Device and services](device.md#motion-and-sensors-asukasensors) |
| `asuka/purchase` | In-app purchases: `declare(items)`, `products(items)`, `buy(item, options)`, `Transaction.finish()`, `events()` (a stream from the app's start), `owned()` (tracked: `has(id)`), `entitlements()`, `restore()`, `statuses(group)`, `manageSubscriptions(id)`, `redeemCode()`, `requestRefund(t)`, `storefront()`. StoreKit 2, Play Billing, the Microsoft Store, the Digital Goods API. [In-app purchases](purchases.md) |
| `asuka/pay` | Apple Pay and Google Pay: `available(request)`, `pay(request)` with async handlers for the shipping contact, method, card, coupon and authorization (an `Update`, or `throw Invalid(field:, message:)`), `setUp()`, the wallet's `PayButton`. PassKit, play-services-wallet, the Payment Request API and Google's pay.js |
| `asuka/appleauth` | Sign in with Apple: `signIn(scopes:, nonce:, state:)`, `credentialState(user)`, `available()`, Apple's `SignInButton`. iOS and macOS |
| `asuka/nfc` | NFC tags: `state()` (tracked), `start(alert:, poll:)` for a `Session` (`next()`, `setAlert`, `fail`, `stop`), `Tag`s (`message`, `ndef`, `write`, `makeReadOnly`, `transceive`), `opened()`, NDEF `Message`s and `Record`s (`encode()`, `decode(bytes)`). Core NFC, Android's reader mode, Web NFC. [NFC tags](nfc.md) |
| `asuka/bluetooth` | Bluetooth LE: `state()` (tracked), `request()`, `scan()` (a stream), `choose(filters)`, `connect(device)` (a `Connection` store: `state`, `services()`, `characteristic(s, c)`), characteristics' `read`, `write`, `notifications()`; `serve(services)`, `advertise(…)`; the SIG's UUIDs. Core Bluetooth, Android's BLE, Web Bluetooth. [Bluetooth](bluetooth.md) |

Where a platform has no code for a native package (an app run by
`asuka test`, which has none), what needs it throws an `unsupported`
error; `asuka/store` keeps nothing then, and reads give their defaults.

The device's packages (`location`, `sensors`, `purchase`, `pay`,
`appleauth`, `nfc`, `bluetooth`) have fakes for tests: `location.fake()`,
`purchase.fake(products)` and the others give the package an Asuka
implementation a test drives (it moves the device, approves a purchase,
taps a tag, adds a peripheral) in place of the platform's, as the
[standard library's README](../std/README.md#test-fakes) says.

## Packages that aren't here

What the standard library doesn't have comes from a
[package](packages.md) (`asuka add`), or from a native package of your
own ([Native code](native.md)) when it's a platform API nothing wraps.
