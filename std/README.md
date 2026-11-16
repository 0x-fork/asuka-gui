# The standard library

Each directory here is a package apps import as `asuka/<name>`
(docs/stdlib.md lists them). The CLI carries this directory (package
`std`, std.go embeds it), so an installed CLI needs no checkout; the
compiler resolves `asuka/<name>` to `std/<name>` (lang/compile).

A package is one of two kinds:

- **Plain Asuka**, with functions the runtime implements declared as
  `native fn` (no body: the builtin `<name>.<fn>`, implemented in C in
  core/src; see lang/LOWERING.md, "The standard library's native fns"):
  time, url, regex, encoding, crypto, fs, ...
- **A native package** (docs/native.md), for what only the platform can
  do: its `native interface`s (modules) and `native component`s (views),
  implemented by each platform in its own language. `std/network` is the
  model to follow.
- **Plain Asuka over a host's own module**, for what a host does as the
  platform's host (the Dock tile and the taskbar, global shortcuts, the
  tray, start at login, recent documents, the installed apps): the hosts
  already answer these by name (`modules["dock"]` in each host), and
  `asuka/platform`'s `call` and `send` reach them, with the module's
  little-endian encodings (`platform.u8`, `u32`, `f64`, `str`, the
  `read*`s) or JSON. `std/dock`, `std/login`, `std/recent`, `std/hotkey`
  and `std/apps` are such; a host that has no such module answers "no
  module", which the package turns into its `unsupported` error or a
  fallback. Events a host reports come back through a long poll (a
  module's `next`) in a `Stream`, shared through `live()` (std/hotkey).

## A native package, as std/network does it

```
network/
  asuka.toml                name = "asuka/network"; what it needs of an app
                            ([ios] usage strings and background modes,
                            [android] permissions), merged into each app
                            that imports it
  network.asuka             the API apps use, and the native declarations
  Package.swift             the Swift package (iOS and macOS)
  swift/Network.swift       yours: makeMonitor
  swift/Network.asuka.swift generated
  android/build.gradle.kts  the Gradle library module (namespace asuka.<name>)
  android/src/main/kotlin/asuka/network/Network.kt   yours
  web/network.js            yours: export function makeMonitor
  desktop/network_linux.c   yours: network_make_Monitor (_windows.c; a
                            .c without a suffix is both's)
  desktop/network.asuka.h   generated (and network.asuka.c)
```

`go run ./cmd/asuka native generate ./std/<name>` writes the generated
files for each platform whose project exists (create Package.swift,
android/build.gradle.kts, web/ and desktop/ first). Commit them. A
package with no C for Windows or Linux has none of desktop/'s for it,
and answers there as a platform without the package's code does.

### The API

- Write the public API in Asuka, for the app's developer: typed structs
  and enums, `async` for what waits, `throws` for what fails. The native
  interface underneath is private (no `pub`), made once at the
  package's top (`let monitor = try? Monitor()`), and the public
  functions handle a platform with no code for it (none: an
  `unsupported` error, or a sensible fallback as network's "connected").
- What the platform changes over time is a `Stream<T>` built on an
  `async fn watch(changed: fn(T))` method (`Stream(send => { try await
  m.watch(changed: send) })`), and, for what a view shows, a tracked
  function through `live(initial, source)`: the platform is watched only
  while something reads it.
- Views are `native component`s (appleauth's SignInButton, pay's
  PayButton): the compiler makes them Native views, their props on the
  native wire by name, their events back by name (lang/LOWERING.md).
- An error enum's cases carry their message twice: the `///` doc (what
  the platforms' generated errors say) and a `fn message()` (what Asuka
  shows for one the package throws itself).
- What the platform keeps for the app until it's read (a purchase's
  transactions, a region's crossing) is read once by the package and fanned
  out to the streams that read it; what came with no reader is kept for
  the first (std/purchase's `events()`, std/location's `crossings()`).

### Test fakes

An app's tests run on the fake host, with no platform code, so a package
whose module makes no sense without the device (a store, a wallet, a tag,
a peripheral) offers a fake the tests drive, in Asuka:

- `pub fn fake(…) -> FakeX` makes a `pub store FakeX`, keeps it in the
  package's `state faked: FakeX? = none`, and returns it for the test.
- Each public function asks the fake first: `if let f = faked { … }`,
  then the platform (`if let p = platform { … }`), then answers as a
  platform with no code does (`notAvailable`, or a fallback).
- The fake's state is what the test sets and reads (`answer`,
  `outcome`, `products`, `peripherals`, what the app wrote); its `pub fn`s
  are what happens outside the app (`move`, `approve`, `tap`, `notify`,
  `cancel`), which it sends to the package's readers.
- A stream the fake feeds registers its `send` with the fake and waits
  with `await never()` until its reader goes (`defer` unregisters it):
  never a loop of sleeps, which the fake host takes for a ticker.
- An async call that waits for the test (pay's sheet, nfc's next tag) is
  a `Stream` of what happens, read until the answer comes.
- A native component shows on the fake host as its name; a test's tap on
  it is its `press` event.

```asuka
test "a region, crossed" {
  let loc = location.fake()
  loc.answer = .always
  loc.move(35.6812, 139.7671)
  …
}
```

### Each platform

- **What an app needs** is the package's to say, and only an app that
  uses it gets it (cmd/asuka's depManifests and nativeNeeds; the hosts
  have nothing of a package's). asuka.toml's tables are for every app
  that imports the package: `[ios] usage` (purpose strings) and
  `backgroundModes`, `[android] permissions` (`"name:28"` up to an API
  level, `"name|neverForLocation"` with flags). What only part of the
  package needs goes in `[uses.<member>]` tables, with the same keys
  (`[uses.requestAlways.android] permissions = [...]`,
  `[uses.startUpdates.ios] backgroundModes = ["location"]`): an app gets
  them only if its program refers to that member (a function, a type, a
  store; `"Type.name"` for a type's member), from its code or a
  dependency's, as the checker resolved it (appbuild.Uses), not a local
  name that's the same nor a test block's. The files beside the code are
  for every app that imports the package: `iOS.entitlements`,
  `macOS.entitlements` and an `Info.plist` beside Package.swift, the
  Gradle module's `src/main/AndroidManifest.xml` and `build.gradle.kts`
  (its libraries): what one member needs (asuka/location's service and
  receiver) goes in asuka.toml's `[uses]` instead. A package over a
  host's module, which has neither, says the rest in asuka.toml too:
  `[ios] entitlements` (a simulator build's values), `plist` (a key set
  to true, an array joined with the app's, or a string), `hostFlags`
  (the iOS host's parts its module needs, by their compilation
  conditions: `ASUKA_NOTIFY`), `derived` (what the CLI makes of the
  app's own asuka.toml: `icloudKeyValue`, `icloudDocuments`,
  `locationPrecise`); `[macos] hostFlags` (the Mac's Swift);
  `[android] application` and `manifest` (elements of the manifest's
  application and of the manifest), `hostModules` (the host's optional
  Gradle modules, platform/android/asuka-<name>: std/push's `push`);
  `[linux] flatpak` (the Flatpak's finish-args). Test it in cmd/asuka's
  TestStdManifests (a member's as "location.requestAlways"). What the
  app's own asuka.toml asks of a package is the CLI's (cmd/asuka/
  config.go's permissionsFor): Apple Pay's merchant IDs, the `nfc` and
  `bluetooth` tables.
- **Swift** (iOS and macOS: one source, `#if os(iOS)` where they differ):
  move the host's code for the module from platform/ios's AsukaHost (and
  platform/macos) into the package, implementing the generated protocol.
  Values cross to the main thread (where callbacks are called) through
  `DispatchQueue.main` or `@MainActor`.
- **Kotlin**: from platform/android's dev.asuka, implementing the
  generated interface; `AsukaModules.context` is the app's Context.
  Callbacks on the main looper. A runtime permission is asked through
  `AsukaModules.activity` (a ComponentActivity)'s
  `activityResultRegistry`; a receiver, a service or a library's
  manifest entries go in the module's `src/main/AndroidManifest.xml`; a
  library (Play Billing, play-services-wallet) in its
  `build.gradle.kts`.
- **JavaScript**: from platform/web, implementing the methods (async
  methods get an AbortSignal first).
- **C, for Windows and Linux** (macOS runs the Swift; docs/native.md's
  "Windows and Linux: C"): Win32, COM and WinRT through
  `asuka_windows.h`'s helpers (a WinRT interface's slots declared as
  Microsoft's documentation orders them, as the Go did), GLib, GIO and
  GTK through `asuka_linux.h`'s declarations (`G(fn)(…)`, resolved at
  run time; a library of its own, libsecret, libsoup, dlopened by the
  package). Every call comes on the main thread: an async method that
  blocks runs on `asuka_desktop_spawn`'s thread and answers from there;
  GLib's async calls answer on the main context. What two packages
  share goes to platform/desktop (`asuka_fetch`,
  `asuka_desktop_local_file`, the system window, WIC's images).

Once a module is a std package, remove the host's old byte-protocol
module for it (`modules["<name>"]` in each host and its code) and the
sdk/<name> Go package.

### Over a host's module

What a host already does for its apps through a module of its own
(notifications, pushes, iCloud, the photo library...: platform/ios's
AsukaHost, platform/android's dev.asuka, platform/web's modules,
host/darwin, host/windows, host/linux) a package reaches through
`asuka/platform`, as above, with no platform code of its own: it calls
the module's methods by name (`platform.call`, `platform.send`,
`platform.next` for a "next" method's stream), with the bytes
sdk/<name> sent (mostly JSON, `platform.u32` and the like for the
rest), types the answers in Asuka, and answers a host without the
module (`platform.unsupported(e)`) as unsupported or with a fallback.
std/notify, push, icloud, library, photos, contacts, calendar, compose,
periodic, background, liveactivity and widget are such. Moving a
module's code into its package (as above) can come after, one platform
at a time, without changing the package's API.

A package whose work the runtime does is plain Asuka with `native fn`s
in C: std/sqlite (core/src/sqlite) calls the sqlite plugin's library,
which std/sqlite's asuka-plugin.toml (sdk/sqlite's, copied) ships with
the apps that import it (on the web, the page's SQLite worker runs it);
std/crash reads the core's record of the runs (core/src/std/crash.c)
where no host keeps its own (iOS, Android, the web).

### Tests

- Asuka: a UI program under lang/compile/testdata/ui (or a run program
  under testdata/run) that imports the package and runs on the fake host,
  with no platform code: it must behave (fallbacks, errors).
- Swift: compiles for macOS and the iOS simulator (xcrun swiftc against
  platform/macos's runtime, as host/darwin's buildKitchenSwift does).
- C: compiled for Windows and Linux with Zig, and run on Linux by
  platform/desktop's std_test.go (an Asuka program on the test host, in
  the asuka-linux-test image) where it can be driven there.
- The platforms end to end: the e2e suites (`go test -tags e2e ./e2e`),
  where a module can be driven.

### Docs

Add the package's row to docs/stdlib.md, and update the guide that
covers it (docs/device.md, docs/data.md, docs/desktop.md, ...) with
Asuka examples that compile.
