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
| `asuka/js` | `js.eval(script, timeout:)`: JavaScript run off screen in the platform's own engine (WKWebView, Android's WebView, WebView2, WebKitGTK, a sandboxed frame), its value as JSON; `js.JsError.timeout` |
| `asuka/json` | `json.decode<T>(text)` and `json.encode(value, indent:)`: typed JSON, by the compiler's knowledge of `T` |
| `asuka/math` | `sqrt`, `pow`, `floor`, `ceil`, `round`, `sin`, `cos`, `atan2`, `clamp`, `random`, `randomInt`, `seed`, `pi` |
| `asuka/time` | `time.now()` (milliseconds since 1970), `time.monotonic()` (a `Duration` that only goes forward) |
| `asuka/http` | `http.get`, `http.post`, `http.postJSON`, `http.request`: requests through the platform's own client, so proxies, trusted certificates, caches and cookies work as the system's do. A `Response` has `status`, `url`, `headers`, `body`, `text()`, `json<T>()` and `ok()` |
| `asuka/network` | `network.status()` (tracked: `connected`, `type`, `metered`, `constrained`; it watches the network only while something reads it), `network.current()`, `network.changes()` (a stream). NWPathMonitor on iOS and macOS, ConnectivityManager on Android, the Network List Manager on Windows, GNetworkMonitor and NetworkManager on Linux, `navigator.onLine` and the Network Information API on the web |
| `asuka/review` | `review.request()`: the store's rating prompt (StoreKit on iOS and the Mac, Play's in-app review), shown or not on the store's quota; nothing on Windows, Linux and the web. `review.appStoreURL(id)`, `review.playStoreURL(id)` for a "Rate us" button |
| `asuka/secure` | `secure.read(key)`, `secure.write(key, value)`, `secure.remove(key)`: small secrets encrypted by the platform (the Keychain, the Android Keystore, Credential Manager, the user's keyring through libsecret, IndexedDB under a non-extractable WebCrypto key) |
| `asuka/splash` | `splash.hold()`, `splash.hide()`: keeps asuka.toml's launch screen up while the app gets ready (iOS, Android; nothing elsewhere) |
| `asuka/store` | `store.read(key, default)`, `store.write(key, value)`, `store.remove(key)`: small values kept across launches as JSON, in the platform's preferences (UserDefaults, SharedPreferences, `localStorage`, a file on Windows and Linux) |
| `asuka/tracking` | `tracking.request()`, `tracking.current()`: App Tracking Transparency's prompt on iOS and the Mac (`.notAsked`, `.restricted`, `.denied`, `.allowed`); Windows' advertising ID setting; the browser's Global Privacy Control or Do Not Track; `.allowed` on Android and Linux, which have no prompt |

Where a platform has no code for a native package (an app run by
`asuka test`, which has none), what needs it throws an `unsupported`
error; `asuka/store` keeps nothing then, and reads give their defaults.

## Packages that aren't here

What the standard library doesn't have comes from a
[package](packages.md) (`asuka add`), or from a native package of your
own ([Native code](native.md)) when it's a platform API nothing wraps.
