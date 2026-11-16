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
| `asuka/time` | `time.now()` (milliseconds since 1970), `time.monotonic()` (a `Duration` that only goes forward) |
| `asuka/dock` | `dock.setProgress(Progress(value:, state:))` and `dock.requestAttention(.critical)`: the app's icon on macOS's Dock, Windows' taskbar and Linux's launcher (Unity's launcher API, where the desktop has it); nothing on a phone or the web |
| `asuka/hotkey` | `hotkey.Hotkey(hotkey.Shortcut(key:, primary:, shift:, alt:, control:)) { description; on press; on error(e) }`: a global shortcut while it's shown (RegisterEventHotKey, RegisterHotKey, the GlobalShortcuts portal); `.notAvailable` on a phone and the web |
| `asuka/http` | `http.get`, `http.post`, `http.postJSON`, `http.request`: requests through the platform's own client, so proxies, trusted certificates, caches and cookies work as the system's do. A `Response` has `status`, `url`, `headers`, `body`, `text()`, `json<T>()` and `ok()` |
| `asuka/login` | `login.set(on)` (`LoginError.needsApproval` on macOS until the user allows it), `login.enabled()`, `login.openedAtLogin()`: start the app as the user logs in (SMAppService, the Run key, XDG autostart); a phone's app can't |
| `asuka/network` | `network.status()` (tracked: `connected`, `type`, `metered`, `constrained`; it watches the network only while something reads it), `network.current()`, `network.changes()` (a stream). NWPathMonitor on iOS and macOS, ConnectivityManager on Android, the Network List Manager on Windows, GNetworkMonitor and NetworkManager on Linux, `navigator.onLine` and the Network Information API on the web |
| `asuka/platform` | `platform.call(module, method, args)` and `platform.send`: the hosts' own modules, by name, with arguments in their little-endian encodings (`u8`, `u32`, `f64`, `str`, `readU32`, ...). For the standard library's packages; an app reaches its own platform code through [native packages](native.md) |
| `asuka/recent` | `recent.add(path)`, `recent.clear()`, `recent.list()`: the documents the user opened lately, in Open Recent and the Dock menu, the jump list, the recently used files |
| `asuka/store` | `store.read(key, default)`, `store.write(key, value)`, `store.remove(key)`: small values kept across launches as JSON, in the platform's preferences (UserDefaults, SharedPreferences, `localStorage`, a file on Windows and Linux) |

Where a platform has no code for a native package (an app run by
`asuka test`, which has none), what needs it throws an `unsupported`
error; `asuka/store` keeps nothing then, and reads give their defaults.

## Packages that aren't here

What the standard library doesn't have comes from a
[package](packages.md) (`asuka add`), or from a native package of your
own ([Native code](native.md)) when it's a platform API nothing wraps.
