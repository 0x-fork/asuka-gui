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
| `asuka/http` | `http.get`, `http.post`, `http.postJSON`, `http.request`: requests through the platform's own client, so proxies, trusted certificates, caches and cookies work as the system's do. A `Response` has `status`, `url`, `headers`, `body`, `text()`, `json<T>()` and `ok()` |
| `asuka/network` | `network.status()` (tracked: `connected`, `type`, `metered`, `constrained`; it watches the network only while something reads it), `network.current()`, `network.changes()` (a stream). NWPathMonitor on iOS and macOS, ConnectivityManager on Android, the Network List Manager on Windows, GNetworkMonitor and NetworkManager on Linux, `navigator.onLine` and the Network Information API on the web |
| `asuka/store` | `store.read(key, default)`, `store.write(key, value)`, `store.remove(key)`, `store.flush()`, `store.dir()`: small values kept across launches as JSON, in the platform's preferences (UserDefaults, SharedPreferences, `localStorage`, a file on Windows and Linux, written a moment after a change) |
| `asuka/notify` | Local notifications: `request()`, `show(n)`, `schedule(n)` (after `n.after`), `cancel(id)`, `showing()`, `remove(ids)`, `removeThread(t)`, `removeAll()`, `setBadge(n)`; `setCategories` (actions, replies), `setChannels`, `setChannelGroups` (Android); `Notification(…).from(person, conversation)` (messages); `present(decide)` (how one shows while the app is in front); `responses()` (a stream of taps, actions and replies; `r.defer()` keeps the app running for the work). The host's notifications on each platform: UserNotifications, Android's, Windows' toasts, Linux's, the browser's |
| `asuka/push` | `push.register()` (the token: APNs', FCM's, the web's subscription), `push.opened()` and `push.received()` (streams of `Message`s; `Message.from(person, conversation)`) |
| `asuka/watch` | `watch.path(dir, ignore:, recursive:, attributes:)` gives a `Watch`: its `root`, and `events`, a stream of batches of `Event`s (`op`: create, write, remove, rename, attrib, overflow; `path`, `oldPath`). FSEvents, inotify and ReadDirectoryChangesW (internal/fswatch); phones and the web: unsupported |
| `asuka/crash` | `crash.reports()` (a stream: the reports kept, then each as it comes; `Report`: `id`, `kind`, `fatal`, `time`, `message`, `text`, `threads`, `context` with breadcrumbs and values), `breadcrumb(message)`, `set(key, value)`, `record(e)`, `delete(id)`. The desktops' hosts keep the record (sdk/crash); phones and the web: not yet (reports is empty) |
| `asuka/icloud` | iCloud on iOS and macOS: `account()` and `files(scope)` (tracked: as iCloud changes them), `Value(key, default)` (a store kept in iCloud's key-value store), `documents()`, `container(id)`, `readFile`, `writeFile`, `delete`, `rename`, `download`, `evict`, `conflicts(path)`, `resolve(path, keeping:)`. Elsewhere: no account, empty lists, "icloud: not available" |
| `asuka/host` | What the packages above build on: a host's own modules (`host.call(module, method, args)`, `host.next(module, method)`); apps don't need it |
| `asuka/sqlite` | `sqlite.open(name)` (a file in the app's data directory, an absolute path, or `":memory:"`) gives a `DB`: `migrate(steps)`, `exec(sql, args)` and `query<T>(sql, args)` (async, off the main thread; rows decoded straight into `T`, a struct's fields by the columns' names), `watch<T>(sql, args)` (a `Live<T>` store: `rows`, `loading`, `error`; run again as its arguments change and as a table it reads is written), `changed()`, `close()`. SQLite is the sqlite plugin's library (sdk/sqlite), shipped with the app; not on the web yet |
| `asuka/storage` | `storage.Value(key, default)`: a store whose `value` is kept on the device (asuka/store's), tracked and written as it changes; `reset()`; `storage.flush()`, `storage.dirName()` |

Where a platform has no code for a native package (an app run by
`asuka test`, which has none), what needs it throws an `unsupported`
error; `asuka/store` keeps nothing then, and reads give their defaults.

## Packages that aren't here

What the standard library doesn't have comes from a
[package](packages.md) (`asuka add`), or from a native package of your
own ([Native code](native.md)) when it's a platform API nothing wraps.
