# AGENTS.md

Asuka is a UI framework that renders with each platform's own widgets,
driven by fine-grained signals. Apps are written in the **Asuka
language** (docs/language.md; its compiler is Go, in `lang/`, run by the
CLI) and run on a **core in C** (`core/`: the VM, signals, the UI
runtime, layout, ops). **Mobile first: iOS and Android.** Desktop and
the web run on the same core: the Windows host (`platform/windows`, C on
Win32, the core and the app linked into one .exe), the macOS host
(`host/darwin`, AppKit through purego, `internal/darwin`) and the Linux
host (`host/linux`, GTK 4 and libadwaita through purego,
`internal/gtk`), Go driving the C core through its ABI (`host/bridge`),
and the web (`platform/web`, the DOM from JavaScript, the core as
WebAssembly). The Windows and Linux hosts share C in
`platform/desktop`. The Go desktop hosts are being replaced (RFC 0008):
the macOS host in Swift on AppKit (`platform/macos`), the Linux host in
C (`platform/linux`). **Read [DESIGN.md](DESIGN.md),
[RFC 0007](rfcs/0007-c-core-and-asuka-language.md) and [RFC
0008](rfcs/0008-no-go-at-run-time-and-c-ffi.md) before changing code**;
DESIGN.md is the source of truth for the architecture, the RFCs for the
rewrite's milestones, lang/LOWERING.md for how the language compiles.
Work milestone by milestone, in order.

## Hard rules

- **No Go at run time** (RFC 0008). Nothing an app ships is Go: Go is
  the CLI's, the compiler's, the generators' and the tests' language.
  New run-time code is C (the core, the Windows and Linux hosts, a
  package's Windows and Linux code), Swift (Apple platforms), Kotlin
  (Android) or JavaScript (the web). Don't add to the Go desktop hosts
  or package `native`: they're being replaced.
- **No cgo, anywhere.** Go code (the CLI, the compiler, the desktop
  hosts) builds with `CGO_ENABLED=0`; it reaches C through purego and
  `syscall`, and builds C by running a compiler (internal/cbuild).
- **The core never knows a host.** Hosts talk to it through
  `core/include/asuka.h` only. Adding an element means: its kind and
  props in `ops/schema.go` (`go generate ./ops`) when it makes a new
  kind of native view, its keys and events in `lang/prelude/elements.go`
  (`go generate ./lang/prelude`), its behavior in `core/src/ui`, and its
  view in every host (the C fake host first, then iOS and Android, then
  macOS, Windows, Linux and the web).
- **C:** C17, `-Wall -Wextra -Werror`, internal symbols `ak_`, the ABI
  `asuka_`; tests under ASan and UBSan (`go test ./core`).
- **Nothing phone-only in the core's data structures**: no single global
  root, no assumption that the screen size never changes.
- **Main thread.** The core, every host call and every state write run
  on the platform's main thread. Other threads reach it with `ak_post`;
  `spawn` runs values-only work on workers. Never block the main thread.
- **One flush, two crossings.** Effects emit ops into a batch; a flush
  applies the props batch, lays out, applies the frames batch. Never
  cross into Swift or Kotlin per property.
- **Hot paths** (signal propagation, flush, layout, op encoding and
  decoding, the VM's dispatch): no per-op or per-write allocation in
  the steady state; tests count allocations (`ak_alloc_count`).
- **Owners dispose explicitly.** Every effect and task has an owner;
  disposing it stops the effect, cancels the task and runs cleanups.
  Test it whenever you add a construct that makes owners.
- **One source, generated outputs:** the wire (`ops/schema.go`), the
  bytecode (`lang/bytecode/opcodes.go`), the elements
  (`lang/prelude/elements.go`). Edit those, run go generate, commit the
  outputs.

## Commands

```sh
go test ./core                      # the C core's tests (ASan/UBSan) and the VM's; -run 'TestC/Layout': one C test file
go test ./lang/...                  # the compiler: parser, checker, codegen, and Asuka programs run on the VM
go run ./cmd/asuka test [dir]       # an Asuka package's test blocks, on the fake host
go run ./cmd/asuka fmt -w [paths]   # format .asuka files
go generate ./lang/bytecode ./lang/prelude  # the VM's opcodes header; the prelude and the UI's element constants
go test ./internal/cbuild           # the core built for this computer, iOS, Android and the web (ASUKA_CC=zig for Zig)
go build ./... && go vet ./...      # CGO_ENABLED=0: everything
go test ./...                       # signal, layout, ui with host/fake, ops, CLI
go test ./uitest                    # Go apps' UI tests: uitest drives them on host/fake (-uitest.update: their snapshots)
go run ./cmd/asuka test examples/counter  # an example's tests (its test blocks); the examples are Asuka, their stdlib in examples/STDLIB.md
go test ./layout -run CSS -update-known  # after fixing layout: refresh known_failures.txt
go test ./ops -update               # after changing the wire format: refresh golden files
go test ./host/darwin -run Conformance   # what the macOS host draws for the style props, measured (package conformance; go generate ./conformance writes its app)
go test -tags e2e ./e2e -run Conformance # likewise on the simulator, the emulator and headless Chrome (ASUKA_CONFORMANCE_OUT=dir: the captures)
swift test --package-path platform/ios # the Swift decoder against the golden files (macOS)
node --test platform/web                # the JavaScript decoder, likewise
(cd platform/android && ./gradlew :asuka:testDebugUnitTest)  # the Kotlin decoder, likewise (ANDROID_HOME set)
go generate ./ops                   # Go constants, Swift, Kotlin and JavaScript decoders
go run platform/web/gen_symbols.go  # the web host's symbols, from the Android host's
go generate ./i18n                  # CLDR's tables, from the pinned release
go test ./i18n -run 'CTables|CompileCases' -update-c  # the C core's CLDR tables (core/src/ui/i18n_gen.c) and its catalog test data
go generate ./sdk/sqlite        # the sqlite plugin's library for every platform, into sdk/sqlite/build (Xcode, the NDK, Zig)
go run ./cmd/asuka plugin publish sdk/sqlite  # upload a plugin's build to its GitHub release, record it in its asuka-plugin.toml
go run ./cmd/asuka i18n check examples/i18n  # an app's catalogs, checked (the compiler reads them: dev and build check them too)
go run ./cmd/asuka native generate examples/media/segmented  # a native package's glue: Go, Swift, Kotlin, JavaScript (dev and build do it too; `native check`: CI)
go test ./internal/nativegen -update     # after changing the generator: refresh its golden files (testdata/kitchen)
go test ./host/darwin -run SwiftNatives  # a native package's Swift (the kitchen), built and called through the Mac's library
go test -tags e2e ./e2e -run NativePackages  # native packages on the simulator, the emulator and headless Chrome
go run ./cmd/asuka dev --platform ios --lang ar examples/i18n  # a language for one run; --lang ar-XB, en-XA: pseudo locales; --dir rtl: any app right to left
go run ./cmd/asuka dev --platform ios examples/counter       # iOS simulator (Xcode): the player (the core without an app) loads the bytecode asuka dev sends
go run ./cmd/asuka dev --platform android examples/counter   # Android emulator (SDK + NDK), likewise
go run ./cmd/asuka dev --platform ios --once examples/counter # build the player, launch it once with the app, without watching
go run ./cmd/asuka run file.asuka                # one file: its test blocks, then what its app shows (fake host)
go test ./internal/appbuild ./cmd/asuka          # the builds for every target, the dev loop end to end (mac, Chrome, simulator, emulator)
go test -tags e2e ./e2e -run IOS                 # GUI tests: the examples on the simulator (Android: -run Android, an emulator running)
go run ./cmd/asuka build --platform ios --simulator examples/counter    # a release build (--platform ios alone: a signed .ipa)
go run ./cmd/asuka build --platform android examples/counter           # an .aab (ASUKA_KEYSTORE, ASUKA_KEY_ALIAS, ASUKA_KEYSTORE_PASSWORD)
go run ./cmd/asuka publish --platform ios --groups Friends examples/counter  # build and upload to TestFlight (ASUKA_ASC_KEY_ID, ASUKA_ASC_ISSUER_ID, ASUKA_ASC_KEY)
go run ./cmd/asuka publish --platform android --track internal examples/counter  # build and upload to Google Play (ASUKA_PLAY_SERVICE_ACCOUNT)
go run ./cmd/asuka dev examples/counter            # macOS (on a Mac, no --platform: mac): the app and its AppKit host in one process, rerun on save
go run ./cmd/asuka build examples/counter          # likewise: a universal .app (--sign "Developer ID Application: ..."; default ad hoc)
go test ./host/darwin                            # the macOS host, driving real AppKit views (macOS)
go test ./host/darwin -run Swift               # an app's macos/*.swift, compiled and loaded by the host (Xcode's command line tools)
(cd examples/counter && ASUKA_SNAPSHOT=/tmp/c.png ASUKA_DUMP=1 ASUKA_SCRIPT="tap:Add" go run .)  # macOS: steps, then the window as a PNG (the window server's) and its view tree, then quit
go run ./cmd/asuka dev examples/counter            # Windows (on a PC, no --platform: windows): the app and its Win32 host in one process, rerun on save
go run ./cmd/asuka build --platform windows examples/counter  # a folder: the .exe (icon, manifest, version) and its assets; from any system
go test ./platform/windows                       # the Windows host (C), its apps built with Zig and driven in Windows (on Windows, or the swiftvm VM ASUKA_WINDOWS_VM names: "Windows Fresh")
go test ./platform/desktop                       # the C the Windows and Linux hosts share, on this computer (ASan, UBSan)
xvfb-run -a dbus-run-session go test ./host/linux  # the Linux host, driving real GTK windows (GTK 4.14, libadwaita 1.5)
go run ./cmd/asuka dev examples/counter            # Linux (on Linux, no --platform: linux): the app and its GTK host in one process, rerun on save
go run ./cmd/asuka build --platform linux examples/counter    # an install prefix, a .tar.gz and a Flatpak manifest; from any system
go run host/linux/gen_symbols.go                 # the Linux host's fallback symbols, from the web host's
go test -tags e2e ./e2e -run Linux               # GUI tests: the examples under Xvfb (Linux)
$env:ASUKA_SNAPSHOT='c.png'; $env:ASUKA_DUMP='1'; $env:ASUKA_SCRIPT='tap:Add'; .\examples\counter\dist\Counter\Counter.exe  # Windows (PowerShell): steps, then the window as a PNG and its view tree
go run ./cmd/asuka dev --platform web examples/counter       # the web: served to the browser, reloaded on save (--lan: to a phone)
go run ./cmd/asuka build --platform web examples/counter     # a static site, in examples/counter/dist/web
go test -tags e2e ./e2e -run Web                  # GUI tests: the examples in headless Chrome
go run ./cmd/asuka update keygen                  # the key pair that signs desktop apps' updates (asuka.toml's updates)
go run ./cmd/asuka publish --platform linux examples/counter  # a desktop build and its signed update, uploaded (GitHub, S3)
go test -tags e2e ./e2e -run Updater               # a desktop app updates itself and relaunches (macOS; Linux under xvfb-run)
```

## Conventions

- Tests assert op streams from `host/fake` and count work (ops,
  measurements, allocations). They never pass or fail on wall-clock
  time: wait for the condition, count the work.
- Prefer the platform's behavior and defaults; an app that sets no style
  must look native (UIKit on iOS, Material 3 on Android). When unsure
  how a platform behaves, read its docs or an established framework's
  implementation (React Native, react-navigation, Solid, Taffy, the CSS specs) before
  inventing one.
- Swift host: UIKit only, no SwiftUI. Kotlin host: Android Views and
  Material Components, no Compose. macOS host: AppKit only, from Go
  through `internal/darwin` (purego), no cgo, no SwiftUI; an app's own
  Swift (`macos/*.swift`) is compiled with `platform/macos` into a
  library the host loads through purego (`host/darwin/swift.go`), never
  linked. Windows host: C17 on
  Win32, the common controls v6, GDI+'s flat API, DWM, Direct3D 11,
  WebView2 and WinRT through COM vtables in C (`platform/windows/src`),
  no WinUI; the controls drawn as Fluent's in custom draw, so an app
  that sets no style looks like Windows 11. Linux host:
  GTK 4 and libadwaita widgets from Go through `internal/gtk` (purego),
  no cgo, no GtkBuilder or Blueprint; an app that sets no style looks
  like a GNOME app. Web host:
  the DOM from plain JavaScript modules in `platform/web/src` (no build
  step, no framework), the browser's own elements for controls and
  dialogs, driven by the core as WebAssembly (`src/core.js`): batches, measures and
  events.
- Every prop that shows text is a binding, so a message said in it
  (`tr.title()`) follows the app's language; only what's shown once (an
  alert) takes a string. Frames are always physical: a host never mirrors
  what the core places.
- The docs have a Chinese translation in `docs/zh` and a Japanese one in
  `docs/ja`, the same pages with the same headings (each one's
  TRANSLATING.md: its style and terms). A change to a page of `docs`
  changes its translations too, written as natural Chinese and Japanese,
  not word for word.
- Keep generated files (`ops` decoders, the core's CLDR tables and catalog
  test data (`go test ./i18n -run 'CTables|CompileCases' -update-c`), native
  packages' `*_native.go` and `*.asuka.*`) committed and up to date.
- Platform code an app needs is a native package (docs/native.md): a Go
  interface or props struct, implemented in each platform's own project
  (Package.swift, an android/ Gradle module, web/, Go on the desktop).
  Changing `internal/nativegen` or package `native`'s wire means
  regenerating every native package in the repo (`asuka native generate`
  from the root) and the platforms' runtimes (NativeModules.swift, kept
  identical in platform/ios and platform/macos; NativeModules.kt;
  native.js).
