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
| `asuka/appearance` | `appearance.current()` (tracked: `accent` as a `Color?`, `increaseContrast`, `reduceTransparency`; it watches the settings only while something reads it), `appearance.now()`, `appearance.changes()` (a stream). The accent is the Mac's, Windows's, the settings portal's on Linux, Material You's on Android, CSS's AccentColor; iOS has none |
| `asuka/biometric` | `biometric.check()` (`kind`, `enrolled`, `passcode`), `biometric.authenticate(reason:, cancel:, noPasscode:)`: LocalAuthentication, BiometricPrompt, Windows Hello, WebAuthn's platform authenticator; `BiometricError.canceled`, `.notAvailable`, `.failed` |
| `asuka/device` | `device.info()` (`model`, `system`, `systemVersion`, `appVersion`, `build`), `device.browse(url)` (in the app: SFSafariViewController, a Custom Tab), `device.pickPhotos(max:)`, `device.pickFiles(types:, multiple:)` (the system's pickers, no permission; copies in the app's caches), `device.clipboardText()`, `clipboardHas()`, `clipboardImage()`, `copyImage(path)`, `clipboardChanges()` (a stream), `device.keepAwake(on)` |
| `asuka/dialog` | `dialog.open(...)`, `dialog.save(name:, ...)`, `dialog.chooseFolder(...)`, `chooseFolders`: the system's file dialogs (NSOpenPanel and NSSavePanel, the common item dialogs, GtkFileDialog, the File System Access pickers), the user's own paths; `dialog.Filter(name:, types:, extensions:)`. Phones: open is the document picker |
| `asuka/http` | `http.get`, `http.post`, `http.postJSON`, `http.request`: requests through the platform's own client, so proxies, trusted certificates, caches and cookies work as the system's do. A `Response` has `status`, `url`, `headers`, `body`, `text()`, `json<T>()` and `ok()` |
| `asuka/network` | `network.status()` (tracked: `connected`, `type`, `metered`, `constrained`; it watches the network only while something reads it), `network.current()`, `network.changes()` (a stream). NWPathMonitor on iOS and macOS, ConnectivityManager on Android, the Network List Manager on Windows, GNetworkMonitor and NetworkManager on Linux, `navigator.onLine` and the Network Information API on the web |
| `asuka/power` | `power.status()` (tracked: `battery`, `onBattery`, `level`, `charging`, `lowPower`, `thermal`), `power.events()`, `suspends()`, `resumes()`, `locks()`, `unlocks()` (streams), `power.idleTime()`: IOKit and ProcessInfo, UIDevice, BatteryManager and PowerManager, GetSystemPowerStatus and the session's notifications, UPower and logind, the Battery Status API |
| `asuka/preview` | `preview.show(sources, start:, edit:)`: files (paths, assets, URLs) in the system's viewer (Quick Look on iOS and the Mac; the file's app elsewhere; a new tab on the web); `preview.PreviewError.noViewer` |
| `asuka/print` | `print.file(source, job:, orientation:)`, `print.html(markup, ...)`: the system's print dialog, a `print.Outcome`; `print.pdf(markup, page:, landscape:, margins:)`: a PDF of markup, its path; `print.Page.a4`, `letter`..., `print.regionPage(locale)`. Its name is the prelude's `print` too: `import asuka/print as printing` |
| `asuka/review` | `review.request()`: the store's rating prompt (StoreKit on iOS and the Mac, Play's in-app review), shown or not on the store's quota; nothing on Windows, Linux and the web. `review.appStoreURL(id)`, `review.playStoreURL(id)` for a "Rate us" button |
| `asuka/screen` | `screen.displays()` (tracked: `id`, `name`, `bounds`, `workArea`, `scale`, `refreshRate`, `primary`), `screen.primary`, `nearest`, `matching`; `screen.lock(orientation)`, `screen.setBars(style:, hideStatus:, hideNavigation:)`, `screen.preventCapture(on)`, `screen.screenshots()` (a stream) |
| `asuka/secure` | `secure.read(key)`, `secure.write(key, value)`, `secure.remove(key)`: small secrets encrypted by the platform (the Keychain, the Android Keystore, Credential Manager, the user's keyring through libsecret, IndexedDB under a non-extractable WebCrypto key) |
| `asuka/shell` | `shell.reveal(path)`, `shell.open(path)`, `shell.trash(path)`, `shell.beep()`: the file manager's (NSWorkspace, Explorer and the Recycle Bin, GIO and FileManager1); `shell.ShellError.noApp` |
| `asuka/splash` | `splash.hold()`, `splash.hide()`: keeps asuka.toml's launch screen up while the app gets ready (iOS, Android; nothing elsewhere) |
| `asuka/store` | `store.read(key, default)`, `store.write(key, value)`, `store.remove(key)`: small values kept across launches as JSON, in the platform's preferences (UserDefaults, SharedPreferences, `localStorage`, a file on Windows and Linux) |
| `asuka/tracking` | `tracking.request()`, `tracking.current()`: App Tracking Transparency's prompt on iOS and the Mac (`.notAsked`, `.restricted`, `.denied`, `.allowed`); Windows' advertising ID setting; the browser's Global Privacy Control or Do Not Track; `.allowed` on Android and Linux, which have no prompt |
| `asuka/webauth` | `webauth.open(url, callbackScheme:, ephemeral:)`: a sign-in in the system's sheet (ASWebAuthenticationSession, a Custom Tab; WebView2 and WebKitGTK windows; a popup), the callback URL; `WebAuthError.canceled`; cancelling the task closes it |

Where a platform has no code for a native package (an app run by
`asuka test`, which has none), what needs it throws an `unsupported`
error; `asuka/store` keeps nothing then, and reads give their defaults.

## Packages that aren't here

What the standard library doesn't have comes from a
[package](packages.md) (`asuka add`), or from a native package of your
own ([Native code](native.md)) when it's a platform API nothing wraps.
