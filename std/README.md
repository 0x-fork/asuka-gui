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
  desktop/network.go        yours (+ _linux.go, _windows.go, _other.go)
  desktop/network_native.go generated
```

`go run ./cmd/asuka native generate ./std/<name>` writes the generated
files for each platform whose project exists (create Package.swift,
android/build.gradle.kts, web/ and desktop/ first). Commit them.

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
- Views are `native component`s.

### Each platform

- **Swift** (iOS and macOS: one source, `#if os(iOS)` where they differ):
  move the host's code for the module from platform/ios's AsukaHost (and
  platform/macos) into the package, implementing the generated protocol.
  Values cross to the main thread (where callbacks are called) through
  `DispatchQueue.main` or `@MainActor`.
- **Kotlin**: from platform/android's dev.asuka, implementing the
  generated interface; `AsukaModules.context` is the app's Context.
  Callbacks on the main looper.
- **JavaScript**: from platform/web, implementing the methods (async
  methods get an AbortSignal first).
- **Go, for Windows and Linux** (macOS runs the Swift): from host/windows
  and host/linux, using internal/windows and internal/gtk directly.
  Calls run on the core's main thread (sync methods) or a goroutine of
  their own (async ones): touch the platform and call callbacks on the
  main thread, with native.Post and native.OnMain. Split by build tags
  (_linux.go is `linux && !android`; an _other.go answers
  errors.ErrUnsupported).

Once a module is a std package, remove the host's old byte-protocol
module for it (`modules["<name>"]` in each host and its code) and the
sdk/<name> Go package.

### Tests

- Asuka: a UI program under lang/compile/testdata/ui (or a run program
  under testdata/run) that imports the package and runs on the fake host,
  with no platform code: it must behave (fallbacks, errors).
- Swift: compiles for macOS and the iOS simulator (xcrun swiftc against
  platform/macos's runtime, as host/darwin's buildKitchenSwift does).
- Go: `go vet` for GOOS=linux and GOOS=windows; tests where the logic is
  more than a call.
- The platforms end to end: the e2e suites (`go test -tags e2e ./e2e`),
  where a module can be driven.

### Docs

Add the package's row to docs/stdlib.md, and update the guide that
covers it (docs/device.md, docs/data.md, docs/desktop.md, ...) with
Asuka examples that compile.
