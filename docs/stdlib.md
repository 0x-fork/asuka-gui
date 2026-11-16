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
| `asuka/http` | `http.get`, `http.post`, `http.postJSON`, `http.request`: requests through the platform's own client, so proxies, trusted certificates, caches and cookies work as the system's do. A `Response` has `status`, `url`, `headers`, `body`, `text()`, `json<T>()` and `ok()` |
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
