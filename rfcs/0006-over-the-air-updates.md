# 0006: Code push on phones

- **Status**: Rejected (for now)
- **Created**: 2026-11-15
- **Related**: 0003 (in-process code patching, rejected for reloads),
  `asuka/sdk/update` (desktop and web updates)

## Summary

Code push: shipping a change to an app's Go code to the iPhones and
Android phones that have it installed, without a store release, as
Shorebird does for Flutter and Expo's updates do for React Native.
Desktop apps already update themselves whole (`asuka/sdk/update`) and a
web app reloads; on phones `update.Supported()` is `ErrUnsupported`.

Two ways were considered and rejected for now. One is the app as
downloaded WebAssembly: on iOS, which allows no JIT, it would run about
50 times slower than the shipped binary under the best interpreter
measured, and 500 to 1,400 times slower under wazero's (measured below).
The other is Shorebird's way:
patched functions run in an interpreter while the rest stays compiled.
That works for Dart because Shorebird owns Dart's compiler and VM. For
Go it would mean an interpreter that isn't verified on iOS or Android
and that gives compiled code new types only by hacking `reflect`.
Asuka's API is generic, which limits what a patch can contain, and
Apple has newly ruled against apps that change after review. This
records the measurements, the store rules, and what would make code
push worth another look.

## Background

### How an Asuka app ships

- **iOS**: the Go code is a `c-archive` linked into the executable and
  signed with the app. iOS maps executable pages only from signed code:
  an App Store app can't load code it downloaded or make memory
  executable (no JIT).
- **Android**: the Go code is `libasuka.so`, loaded by
  `AsukaNative.open`. A debug build already loads
  `files/asuka/libasuka.so` when `asuka dev --once` pushed one, so
  swapping the library is technically easy; Play's policy forbids it.

So on both platforms a code push has to run its code in an interpreter.

### What the stores allow

- **Apple Developer Program License Agreement 3.3.1(B)** (October 2026;
  3.3.2 in older versions): "an Application may not download or install
  executable code. Interpreted code may be downloaded to an Application
  but only so long as such code: (a) does not change the primary purpose
  of the Application … (b) does not bypass signing, sandbox, or other
  security features of the OS; and (c) … does not create a store or
  storefront for other Applications."
- **App Review Guideline 2.5.2**: apps may not "download, install, or
  execute code which introduces or changes features or functionality of
  the app".
- **Guideline 2.5.19** (new, 2026-10-16): "Don't include any hidden,
  dormant, or undocumented features in your app". Apple's announcement
  aims it at apps "changing after approval — for example, adding
  features or functionality that weren't included in the version of the
  app that was submitted to App Review." Bug fixes by interpreted patches
  stay within 3.3.1(B), but the room for anything more is shrinking.
- **Google Play, Device and Network Abuse**: "an app may not download
  executable code (such as dex, JAR, .so files) from a source other than
  Google Play. This restriction does not apply to code that runs in a
  virtual machine or an interpreter where either provides indirect
  access to Android APIs". A new `libasuka.so` is out. Interpreted code
  is in.

### What others do

- **Expo updates (React Native)**: the app's JavaScript is the update; a
  build's `runtimeVersion` (or a fingerprint of its native code) says
  which updates fit. Manifests are signed, and assets are addressed by
  SHA-256. A launch on a new update that crashes in its first seconds
  rolls back to the last good one or the embedded one. JavaScript is
  interpreted by design, so there is nothing to bridge.
- **CodePush** retired with App Center on 2025-03-31. Its successors
  (hot-updater, Revopush, self-hosted servers) push JavaScript bundles
  the same way.
- **Shorebird (Flutter)**: on iOS, "a patch's changed code runs in
  Shorebird's interpreter". A linker step matches the patch to the
  release: "Code the linker can match to the release keeps running on
  the CPU. Changed code, and code that depends on it, runs in the
  interpreter". Typical patches keep 95% of code linked (Flutter 3.44);
  the worst tenth keep 77%. On Android, macOS, Linux and Windows patches
  aren't linked: the patched snapshot runs natively. A patch changes
  only Dart code, for one release, never native code or assets. It is
  signed, and a boot that fails rolls back. Shorebird forked Dart's
  compiler and VM to do this. The interpreter shares the VM's heap,
  types and calling convention with the AOT code.

## Option 1: the app as downloaded WebAssembly

The React Native model maps onto Asuka on paper. The player
(`mobile/player`) is already a native shell that takes any app's Go
code over the host protocol (`internal/remote`), native packages
included. If it ran the app's Go code, compiled for `GOOS=wasip1`, in
an embedded runtime and spoke that protocol over an in-memory pipe, an
update would be a new `.wasm`.

The cost is interpretation. An App Store app can't map memory
executable, so on iOS the `.wasm` must be interpreted. wazero, the
pure-Go runtime, has no compiler on `GOOS=ios` or `GOOS=android` at all.
Two things are slow, and they are measured apart below: Go's
WebAssembly itself, and the runtime that runs it. These are Asuka's own
benchmarks, with the core and the app together as they would run in
that `.wasm`. They were run on an Apple Silicon Mac (Go 1.27.3,
`-benchtime 1s`), so a phone would be slower in every column. Each
cell gives the time and how many times slower it is than native.

| benchmark | native | V8 (JIT) | wazero compiler | WAMR fast interpreter | wasm3 | wazero interpreter |
|---|---:|---:|---:|---:|---:|---:|
| signal DeepPropagation | 1.23 µs | 3.2 µs (2.6×) | 8.8 µs (7×) | 44 µs (36×) | 34 µs (28×) | 920 µs (750×) |
| signal BroadPropagation | 2.15 µs | 8.8 µs (4.1×) | 29 µs (14×) | 127 µs (59×) | 104 µs (49×) | 1.41 ms (655×) |
| signal Diamond | 110 ns | 485 ns (4.4×) | 2.06 µs (19×) | 6.8 µs (62×) | 5.4 µs (49×) | 57 µs (520×) |
| ui Mount1000 | 5.7 ms | 20 ms (3.5×) | 67 ms (12×) | 334 ms (58×) | n/a | 4.46 s (776×) |
| ui UpdateOne1000 | 0.16 ms | 0.47 ms (2.9×) | 1.9 ms (12×) | 6.6 ms (40×) | n/a | 115 ms (705×) |
| ui UpdateAll1000 | 0.96 ms | 3.7 ms (3.9×) | 11.8 ms (12×) | 64 ms (66×) | n/a | 1.05 s (1,085×) |
| layout RelayoutOneText | 15.5 µs | 56 µs (3.6×) | 591 µs (38×) | 694 µs (45×) | 588 µs (38×) | 15.3 ms (985×) |
| layout FullLayout | 79.5 µs | 322 µs (4.0×) | 2.16 ms (27×) | 5.1 ms (64×) | 4.9 ms (62×) | 116 ms (1,460×) |

The runtimes used:

- **V8**: Node 26's `node:wasi`, an optimizing JIT. This is the floor:
  what Go's WebAssembly costs with the best compiler behind it.
- **wazero 1.12.0**: `wazero run` and `wazero run -interpreter`.
- **WAMR 2.4.5**: built with its fast interpreter, without JIT or AOT.
  It is C, which `mobile/` could link through cgo.
- **wasm3**: built from main. It can't compile `ui`'s module ("compiling
  function overran its stack height limit"), and it is in minimal
  maintenance.

To reproduce, build a package's tests as wasm with
`GOOS=wasip1 GOARCH=wasm go test -c -o ui.wasm ./ui`. Then run it with
`-test.run='^$' -test.bench=.` under each runtime, the package's
directory mounted as `/`.

So both are slow, and the runtime decides most of it:

- **Go's WebAssembly costs 3 to 4.5 times native even under V8.** Go
  keeps its own stack in linear memory, and it compiles each function
  so that it can stop and resume at its calls, which goroutines need
  because WebAssembly has no way to switch stacks. TinyGo's output is faster, but TinyGo doesn't
  support all of Go, and it wasn't measured.
- **A good interpreter costs another 10 to 15 times on top.** The best
  interpreters measured (WAMR, wasm3) are 30 to 65 times slower than
  native.
- **wazero's interpreter is 15 to 25 times slower than those.** It is
  "a naive interpreter-based implementation", in wazero's own words.
  The 500 to 1,400 times measured first was mostly wazero.

Even with WAMR, the whole app as interpreted WebAssembly runs about 50
times slower than the release it patches, until the next store release
replaces it. On a Mac, updating one row in a thousand takes 6.6 ms, most
of a 120 Hz frame, against a sixth of a millisecond today. Mounting a
thousand rows takes a third of a second, against 6 ms. A screen with
little in it would be usable, while lists and large screens would drop
frames. An app that took an update would run worse than one that
didn't, which is the opposite of what an update is for.

Moving only the app, or only the changed modules, into WebAssembly and
keeping the core native doesn't help either. The core's hot paths call
the app's closures (signals' computations, effects, list cell
builders), so every call would cross the boundary with its values
copied, since a WebAssembly module's Go heap isn't the host's. Signals,
closures and nodes are Go values that one heap can't hand to the other.

## Option 2: patched functions interpreted, the rest compiled

This is Shorebird's way, adapted to Go. It is what code push for Asuka
would have to be.

### How it would work

- **The release is kept.** `asuka build` writes a record beside the
  `.ipa` or `.aab`: the app's packages' source and types, the Go version,
  and a table of the functions and generic instantiations the binary
  links. A patch may call only what is in that table.
- **Every app function can be redirected.** The release build's overlay
  rewrites the app's packages, as `asuka dev`'s already does for
  `signal.KeepPlaces`. Each function and function literal gets a
  prologue, `if p := patch.At(k); p != nil { return p.Call(args,
  captured...) }`. Here `k` is the place's name from the table
  `asuka dev` already makes (`Screen>ui.Show#1`), which stays stable
  across edits elsewhere. A literal passes pointers to what it captures,
  so a patched closure shares the compiled function's variables. That
  reaches closures, which RFC 0003 found go-hotfix can't. Unpatched, a
  prologue costs a load and a branch.
- **`asuka patch` compiles the patch.** It diffs the app against the
  release's record function by function. Each changed body (and, as
  Shorebird does, its callers, conservatively) becomes interpreter code:
  SSA from `golang.org/x/tools/go/ssa`, which ixgo interprets. The patch
  is signed with the key desktop updates use (`asuka update keygen`) and
  keyed to the release's build. It is delivered through the manifests,
  rollouts and channels that `asuka/sdk/update` already has. A launch
  that crashes before its first screen stays up rolls back (internal/crashrun
  already records a run before the app's `main`).

### Why it doesn't fit, today

- **No interpreter fits.** ixgo (v1.2.6) runs Go 1.25 to 1.27 with
  generics, but it lists macOS, Linux, Windows and WebAssembly as its
  platforms, not iOS or Android. yaegi's standard-library tables stop
  at Go 1.22, and its generics have open bugs. Either one calls compiled
  code through `reflect`, which DESIGN.md's hot-path rules exist to
  avoid, on the paths a patch would sit on.
- **Types are the hard part.** A patch that adds a type with methods
  compiled code sees through an interface (a `ui.Node`, an
  `fmt.Stringer`) needs reflectx's method-set hacks. Those work from a
  fixed table of pregenerated stubs, or need a patched toolchain.
  Shorebird has no such seam because its interpreter is part of the VM.
  For Go it would mean maintaining a Go toolchain fork, or limiting
  patches to changed function bodies over the release's existing types.
- **Asuka's API is generic.** `signal.New`, `ui.Val`, `ui.For` and
  `ui.List` have hundreds of uses in the examples alone. A patch could
  instantiate them only with type arguments the release already linked:
  Go can't stencil a generic function at run time.
- **What a patch couldn't change** grows with each of these: struct
  fields and method sets, new instantiations, functions the linker
  removed, new imports, native packages' interfaces and glue, `ios/`
  and `android/` code, `go:embed` files, `init` order. Any patched code
  on a hot path (a list cell builder, a memo) would run at the
  interpreter's speed.
- **Two semantics in production.** RFC 0003 rejected an interpreter for
  development partly because a bug could be the interpreter's. Shipping
  one to users makes that a production risk, on a path that only runs
  after a patch, which tests of the release never cover.
- **A cost on every build.** Every build would carry the interpreter,
  the prologues, and the symbol table, which keeps code the linker would
  otherwise remove, whether a patch ever ships or not.
- **The rules point the other way.** Apple's 2.5.19 targets apps that
  change after review, and Play's exception covers interpreted code, not
  the native code an Asuka app is. A logic fix waits for review, which
  is usually a day; an expedited review is available for a critical
  bug.

## What would change this

- A Go interpreter that runs on iOS and Android, calls compiled code
  without `reflect`, and keeps up with Go's releases. Or Go itself
  gaining a supported way to load code that isn't linked in.
- Apps that keep shipping fixes that couldn't wait for review, and a
  survey of those fixes showing they'd fit option 2's limits: function
  bodies only, over the release's types and instantiations.
- On Android alone, outside Google Play (enterprise or MDM distribution,
  other stores): swapping `libasuka.so` is already how debug builds load
  the app's code, so a signed native update there is small. It isn't
  proposed, because Play is where most apps ship.

## References

- [App Review Guidelines](https://developer.apple.com/app-store/review/guidelines/)
  (2.5.2, 2.5.19, 4.7), and [the 2.5.19 announcement](https://developer.apple.com/news/?id=vvzx6cul).
- [Apple Developer Program License Agreement](https://developer.apple.com/support/terms/apple-developer-program-license-agreement/), 3.3.1(B).
- [Google Play: Device and Network Abuse](https://support.google.com/googleplay/android-developer/answer/9888379).
- [Shorebird: how we built code push](https://shorebird.dev/blog/how-we-built-code-push/),
  [performance](https://docs.shorebird.dev/code-push/performance/),
  [ongoing performance work](https://shorebird.dev/blog/shorebirds-ongoing-performance-work),
  [the updater](https://github.com/shorebirdtech/updater/blob/main/library/README.md).
- [Expo updates: how it works](https://docs.expo.dev/eas-update/how-it-works/),
  [error recovery](https://docs.expo.dev/eas-update/error-recovery/),
  [the protocol](https://docs.expo.dev/technical-specs/expo-updates-1/).
- [App Center's retirement](https://learn.microsoft.com/en-us/appcenter/retirement).
- [wazero](https://github.com/tetratelabs/wazero) (`internal/platform`: the
  compiler's platforms), [WAMR](https://github.com/bytecodealliance/wasm-micro-runtime),
  [wasm3](https://github.com/wasm3/wasm3).
- [ixgo](https://github.com/goplus/ixgo), [reflectx](https://github.com/goplus/reflectx),
  [yaegi](https://github.com/traefik/yaegi).
