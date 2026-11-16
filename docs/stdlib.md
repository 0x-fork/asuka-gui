# The standard library

The standard library is the `asuka/...` packages. They come with the
`asuka` CLI, so they need no `asuka add`: import one and use it.

```asuka
import asuka/http
import asuka/store

state units = store.read("units", "metric")
```

Most need no platform code: they're Asuka, and what Asuka can't say is
the runtime's own C, so they work the same everywhere (`asuka/json` and
`asuka/math` are part of the compiler). The others are
[native packages](native.md) like any other: each platform runs them
with its own APIs (URLSession on iOS and macOS, Android's own classes,
the browser's on the web, Go on Windows and Linux), so they behave as
they do for the platform's own apps.

| Package | What it has |
|---|---|
| `asuka/json` | `json.decode<T>(text)` and `json.encode(value, indent:)`: typed JSON, by the compiler's knowledge of `T` |
| `asuka/math` | `sqrt`, `pow`, `floor`, `ceil`, `round`, `sin`, `cos`, `atan2`, `clamp`, `random`, `randomInt`, `seed`, `pi` |
| `asuka/time` | `time.now()`, `time.date(2026, 3, 7, hour: 9, zone: .utc)`, `time.unix(s)`, `time.parse(rfc3339)`: a `Time`, an instant with its zone (`.local`, `.utc`, `.fixed(seconds:)`) and its wall clock's `year`, `month`, `day`, `hour`, `minute`, `second`, `nanosecond`, `weekday`. Its methods: `format("EEE d MMM, HH:mm")` (Unicode's date patterns, in English), `rfc3339()`, `add(d)`, `sub(t)` (a `Duration`), `addDate(years:, months:, days:)`, `isBefore`, `isAfter`, `compare`, `sameDay`, `startOfDay`, `truncate(d)`, `inZone`, `utc`, `local`, `unix`, `unixMilli`, `unixNano`. `time.monotonic()` (a `Duration` that only goes forward), `time.since(t)`, `time.ticks(1s)` and `time.ticker(1s)` (a stream of times) |
| `asuka/url` | `url.parse(text)`: a `URL?` of `scheme`, `user`, `host`, `port`, `path`, `rawQuery`, `fragment`, with `query(name)`, `queryAll(name)`, `text()` and `resolve(ref)` (RFC 3986); `url.resolve(base, ref)`, `escape`, `unescape`, `pathEscape`, `pathUnescape`, `encodeQuery(map)` (names sorted), `parseQuery(raw)` |
| `asuka/regex` | `regex.compile(pattern)` (it throws a bad pattern): a `Regex` with `matches`, `find`, `findAll`, `findRange`, `findSubmatch`, `findAllSubmatches`, `replace(text, with: "$1")`, `replaceEach(text, f:)`, `split`, `groupNames`; `regex.quote(text)`. RE2's syntax, matched in linear time (no backtracking) |
| `asuka/encoding` | `base64Encode`, `base64URLEncode` (unpadded), `base64Decode` (either alphabet), `hexEncode`, `hexDecode` |
| `asuka/crypto` | `sha256`, `sha1`, `hmacSHA256(key:, data:)`, `randomBytes(n)` (the system's randomness), `uuid()` (version 4), `equal(a, b)` (in constant time) |
| `asuka/html` | `escape(text)` and `unescape(text)` (numeric references and the named ones text uses) |
| `asuka/random` | `random.int(1, 6)` (both ends included), `float()`, `chance(p)`, `pick(items)`, `shuffled(items)`, `seed(n)` |
| `asuka/fs` | `fs.appDir()` (the app's own directory), `join([…])`, `clean`, `dir`, `base`, `ext`; and calls that block, for `spawn { }`: `readDir` (an `Entry`: `name`, `isDir`, `size`, `modified`), `stat`, `read`, `readText`, `write`, `writeText`, `append`, `appendText`, `copy`, `rename`, `mkdirAll`, `remove`, `removeAll`, `exists`. A failure throws an `FsError` (`.notFound(path)`, `.exists`, `.denied`, `.notDirectory`, `.isDirectory`, `.notEmpty`, `.failed(path, reason)`) |
| `asuka/apps` | `apps.list()`, `apps.search(apps, query)` (a launcher's ranking: the name, its start, a word's start or its words' initials, anywhere, its letters in order; case and accents aside), `apps.icon(app, size)` (a PNG's path), `apps.open(app)`: the installed apps on macOS, Windows and Linux; `AppsError.unsupported` on a phone and the web |
| `asuka/dock` | `dock.setProgress(Progress(value:, state:))` and `dock.requestAttention(.critical)`: the app's icon on macOS's Dock, Windows' taskbar and Linux's launcher (Unity's launcher API, where the desktop has it); nothing on a phone or the web |
| `asuka/hotkey` | `hotkey.Hotkey(hotkey.Shortcut(key:, primary:, shift:, alt:, control:)) { description; on press; on error(e) }`: a global shortcut while it's shown (RegisterEventHotKey, RegisterHotKey, the GlobalShortcuts portal); `.notAvailable` on a phone and the web |
| `asuka/i18n` | the language the app shows: `i18n.locale()`, `setLocale(tag)`, `languages()`, `preferredLanguages()`, `isRTL()`, `autonym(tag)`, `region()`; numbers and dates its way: `formatNumber`, `formatInt`, `formatPercent`, `formatDate(t, date:, time:)`. The app's messages are `tr.key(…)` (see [Languages](i18n.md)) |
| `asuka/http` | `http.get`, `http.post`, `http.postJSON`, `http.request`: requests through the platform's own client, so proxies, trusted certificates, caches and cookies work as the system's do. A `Response` has `status`, `url`, `headers`, `body`, `text()`, `json<T>()` and `ok()` |
| `asuka/login` | `login.set(on)` (`LoginError.needsApproval` on macOS until the user allows it), `login.enabled()`, `login.openedAtLogin()`: start the app as the user logs in (SMAppService, the Run key, XDG autostart); a phone's app can't |
| `asuka/network` | `network.status()` (tracked: `connected`, `type`, `metered`, `constrained`; it watches the network only while something reads it), `network.current()`, `network.changes()` (a stream). NWPathMonitor on iOS and macOS, ConnectivityManager on Android, the Network List Manager on Windows, GNetworkMonitor and NetworkManager on Linux, `navigator.onLine` and the Network Information API on the web |
| `asuka/platform` | `platform.call(module, method, args)` and `platform.send`: the hosts' own modules, by name, with arguments in their little-endian encodings (`u8`, `u32`, `f64`, `str`, `readU32`, ...). For the standard library's packages; an app reaches its own platform code through [native packages](native.md) |
| `asuka/recent` | `recent.add(path)`, `recent.clear()`, `recent.list()`: the documents the user opened lately, in Open Recent and the Dock menu, the jump list, the recently used files |
| `asuka/store` | `store.read(key, default)`, `store.write(key, value)`, `store.remove(key)`: small values kept across launches as JSON, in the platform's preferences (UserDefaults, SharedPreferences, `localStorage`, a file on Windows and Linux) |

Where a platform has no code for a native package (an app run by
`asuka test`, which has none), what needs it throws an `unsupported`
error; `asuka/store` keeps nothing then, and reads give their defaults.

`asuka test` makes runs repeat: `time.now()` starts at
2026-01-01T00:00:00Z and moves only as the test's time does (`sleep`),
the local zone is UTC, and each test has an empty directory of its own
as `fs.appDir()`. On the web the app has no directory yet, so `asuka/fs`'s
calls throw there.

## Packages that aren't here

What the standard library doesn't have comes from a
[package](packages.md) (`asuka add`), or from a native package of your
own ([Native code](native.md)) when it's a platform API nothing wraps.
