# 0008: No Go at run time, and C from Asuka

- **Status**: Accepted (in progress, see "Milestones")
- **Created**: 2026-11-16
- **Related**: 0007 (the C core and the Asuka language), DESIGN.md,
  docs/native.md

## Summary

Nothing that runs on a user's device is Go. The phones and the web
already run the C core with hosts in Swift, Kotlin and JavaScript; the
desktops move off Go too:
- the macOS host is Swift on AppKit (platform/macos, beside
  platform/ios);
- the Windows and Linux hosts are C (platform/windows,
  platform/linux).

A native package's desktop code is Swift on macOS and C on Windows
and Linux, and what the Go desktop runtime did (the self-updater,
crash records, window and route state, single instance) moves into the
hosts.

Go stays where nothing ships: the CLI, the compiler, code generators,
and test tooling.

Asuka gains a **C foreign function interface**: `extern` declarations
call C functions, from the app or from a package's own C, with no
libffi and no per-call marshalling beyond what the types need.

## Motivation

- **One runtime, no second one.** A desktop app today carries the Go
  runtime and its garbage collector beside the C core: about 10 MB
  more of binary, its own threads, a heap that gives memory back
  slowly, and a startup that loads both. The C core was written so
  that an app is the core, its compiled code and a thin host; the
  desktops should be no different from the phones.
- **Package authors write the platform's language.** Swift on Apple
  platforms, Kotlin on Android, JavaScript on the web, and today Go on
  Windows and Linux, a language neither platform's documentation
  speaks. C is what Win32, COM, GTK and GLib are written for.
- **C is the common denominator.** Every library an app may want
  (SQLite, a codec, a vendor SDK) has a C API. An FFI in the language
  reaches all of them on every platform, without a native package's
  ceremony for what is one function call.

## Decisions

| Question | Decision | Why |
|---|---|---|
| macOS host | Swift + AppKit, a Swift package in platform/macos; the decoder shared with iOS's | The iOS host's structure carries over; package code is already Swift on the Mac. |
| Windows host | C17, Win32, common controls, GDI+'s flat API, DWM, WebView2 and WinRT through COM vtables | Win32 is a C API; COM is C-callable; no runtime. |
| Linux host | C17, GTK 4.14 and libadwaita 1.5, GLib, GIO (D-Bus, portals) | GTK is C. |
| Building the Linux host | Our own declarations of the subset of GTK, GLib and libadwaita it uses; their symbols resolved with dlopen and dlsym as it starts | It builds from any system with Zig (no GTK sysroot), as the Go host did with purego. |
| Building the Windows host | Zig's MinGW target from any system; clang or MSVC on Windows | Cross-builds from a Mac or Linux, as today. |
| An app's executable | The host with the core (and the app compiled to C) linked in: one executable on Windows and Linux, a bundle's executable on macOS | No dynamic library to load; dead stripping across the core and the app. |
| Desktop native packages | macOS: Swift (as now). Windows and Linux: C in the package (`desktop/`, `_windows.c` and `_linux.c` for one), against generated glue | The platforms' own language. |
| C from Asuka | `extern fn` and `extern type` in Asuka, glue the compiler generates in C, compiled into the core's build | One direct call; works on the VM and compiled to C alike. |
| Go | The CLI, the compiler, generators, tests; never in what an app ships | Go is the toolchain's language, not the runtime's. |

## Desktop hosts

Each host implements the ops wire as the phones' hosts do (DESIGN.md).
They call the C ABI (`asuka.h`) and get batches through the
callbacks, but in-process and linked: the host's own C or Swift calls
`asuka_start` with function pointers, with no purego and no
`measure_v` workaround. Where the Go hosts did something well, the new
ones port it, not reinvent it:
- Fluent drawn in custom draw on Windows;
- libadwaita's widgets on Linux;
- AppKit's own controls on macOS;
- core-virtualized lists;
- the menu bar, trays, windows, flyouts, drag and drop, accessibility
  (UI Automation, AT-SPI, NSAccessibility);
- shaders, which the compiler translates (HLSL for Direct3D 11,
  GLSL for OpenGL, MSL for Metal) as it does GLSL for the web.

What the Go runtime's packages did becomes the hosts':

- **The self-updater**:
  - the feed is read and the update downloaded with WinHTTP on
    Windows, libsoup through GIO on Linux, and URLSession on macOS;
  - an Ed25519 signature is checked (a small C implementation shared
    by Windows and Linux; CryptoKit on macOS);
  - the update is staged and relaunched as today.

  `asuka update keygen` and the signing stay in the CLI.
- **Crash records**: the core's (`asuka_crash_start`), shared with the
  phones, plus each platform's way to catch a crash (signals and
  `sigaltstack`, `SetUnhandledExceptionFilter`, Mach exceptions or
  signals).
- **Window and route state, single instance, the app's directories**:
  the same files and formats (`asuka-window.json`,
  `asuka-routes.json`), in each host's language.
- **Under `asuka dev`**:
  - the player is the host without an app compiled in, as on the
    phones;
  - it connects with `asuka_dev_connect`;
  - a change to the app's C or Swift rebuilds it.

Shared C for Windows and Linux (the JSON the state files need, files,
the app's settings and directories, window and route state, flyouts'
places, Asuka's own words, animated images) lives in platform/desktop.
The updater's portable part (the feed, channels, versions, Ed25519,
deltas) is the standard library's `asuka/update`, in Asuka with C
builtins in the core; a host answers only its `update` module (whether
it can install, the download, putting a staged update in place).

## Native packages on the desktop

A package's Windows and Linux code is C:
- `desktop/*.c` in the package: `x_windows.c` and `x_linux.c` for one
  platform, `x.c` for both (`#ifdef _WIN32` where they differ), as most
  packages share much between the two (a snapshot's diffing, a JSON
  store, the codec's use); a platform with no file of its own gets none
  of the package's C;
- `asuka native generate` writes `<pkg>.asuka.h` and `<pkg>.asuka.c`
  beside them, holding:
  - the package's types as C structs and enums;
  - a vtable per `native interface`;
  - the codec, over the core's (core/src/native);
  - a provider the host finds by the package's path, as Swift's
    `AsukaModule_<path>` and Kotlin's `AsukaModule` are found.

The package writes `make<Interface>` functions, as on the other
platforms. An `async` method gets a completion it calls once on the
main thread; a function parameter is a callback the host posts to the
main thread. `native component`s are views the host asks the provider
for by name.

What a package's C may use of the host is one header, `asuka_desktop.h`,
implemented by both C hosts:
- posting to the main thread;
- the window a view is in (`HWND`, `GtkWindow *`);
- resolving a semantic color;
- the app's name, ID and directories;
- the assets' directory.

## C from Asuka

```asuka
// sqlite.asuka: a package's own binding of its C (c/), or of a library
// asuka.toml links.

/// A database handle the C side owns; Asuka calls close as the last
/// reference goes.
extern type Db with release db_close

extern fn sqlite3_libversion() -> CString
extern fn db_open(path: CString) -> Db?
extern fn db_close(db: Db)
extern fn db_exec(db: Db, sql: CString) -> Int32
extern fn db_each(db: Db, sql: CString, row: fn(CString)) -> Int32

pub fn version() -> string = sqlite3_libversion()
```

```toml
# asuka.toml
[c]
sources = ["c/*.c"]
include = ["c/include"]
defines = ["SQLITE_THREADSAFE=0"]
libraries = ["sqlite3"]            # where the platform has it: -lsqlite3, sqlite3.lib
[c.macos]
frameworks = ["Security"]
[c.windows]
libraries = ["winhttp"]
```

- **Types**:

  | Asuka | C |
  |---|---|
  | `Int8` … `Int64`, `UInt8` … `UInt64` | `int8_t` … `uint64_t` |
  | `int` | `int64_t` |
  | `Float32` | `float` |
  | `float` | `double` |
  | `bool` | `bool` |
  | `CString` | `const char *`: an Asuka `string` converts to a NUL-terminated copy for the call, and a returned one is copied into a `string` (`CString?` for a NULL) |
  | `bytes` | a pointer and a length, two parameters |
  | `Pointer` | `void *`, opaque |
  | `extern type T` | an opaque pointer Asuka holds a reference to, released by its `release` function |
  | `extern struct S { … }` | a C struct of those types, by value |
  | `fn(…)` | a C function pointer and its context: the closure, retained while the C side holds it, called on the main thread (from another thread, it's posted) |

- **Glue, not libffi.** For each package with `extern`s, the compiler
  generates C:
  - one wrapper per function, in the core's builtin calling convention
    (`ak_val` arguments), converting and calling;
  - a function that registers them as builtins (`ffi.<package>.<name>`).

  The bytecode calls them with CALLB like any builtin, and the code
  compiled to C calls them the same way. The CLI compiles the glue and
  the package's C sources into the core's build: a release, and a
  development player, which a change to C or to an `extern`
  declaration rebuilds (as a native package's code does), while a
  change to Asuka only reloads.
- **Threads**:
  - an `extern` call runs on the thread that makes it, the main thread
    in a component or a handler;
  - `spawn` runs one on a worker, its arguments values only;
  - a C call that blocks holds the main thread, as any synchronous
    call does.
- **Safety**:
  - pointers are opaque values Asuka never dereferences;
  - memory the C side gives is its own, freed through an `extern type`'s
    release or an explicit call;
  - a `CString` or `bytes` passed in is valid during the call only.
- **Where it works**:
  - every platform: the C is compiled for each target with the core;
  - the web: into the WebAssembly module, where there are no system
    libraries, so a package's `[c]` per platform says what it needs
    there;
  - `asuka test` on the fake host: the C is compiled into
    `asuka-run`.

### Decided in F1

What the sketch above left open, as F1 built it (docs/c.md is the
guide, lang/LOWERING.md's "C from Asuka" the lowering):

- **The C types are written in extern declarations only.** `Int32`,
  `UInt8`, `Float32`, `CString` name C's side; Asuka sees `int`, `float`,
  `string` (`let n: Int32` is an error that says so). `Pointer` is an
  Asuka type of its own (an int-sized value Asuka compares and passes
  back, never reads). `string` in a declaration is an error that says to
  write `CString`.
- **Narrowing wraps.** An `int` passed as an `Int8` converts as C
  converts (300 is 44); a `UInt64` above `int`'s range comes back
  negative. No check, no throw.
- **`T?` is NULL.** `CString?`, `Pointer?`, an extern type's `T?` and, as
  parameters, `bytes?` and `(fn(…))?` pass none as `NULL`; a result of
  one of those is none for `NULL`. A non-optional `CString` or extern
  type result that's `NULL` throws (`db_open returned NULL, and its result
  is a Db`). Scalars have no `?`.
- **A `CString` in is the string's own bytes** (Asuka's strings end with
  a NUL), valid during the call: no copy. **A `CString` out is copied,
  and C's memory isn't freed**: most C APIs return strings they own; one
  that returns memory the caller frees is wrapped in C, or returns a
  `Pointer` freed by another extern fn.
- **`bytes` is two parameters**, `const uint8_t *` and `size_t`, valid
  during the call, read-only. A function doesn't return `bytes`.
- **Extern types** are the runtime's objects (an opaque pointer, its
  release, its type's name). The release function is an extern fn of the
  same package taking one `T`; calling it from Asuka releases at once, and
  the value then holds nothing (a later call with it throws `the Db was
  released`), so it's never freed twice. Without `with release`, nothing
  is freed. They're equal when their pointers are; spawn can't take one.
- **Extern structs** hold numbers, `bool`, `Pointer` and other extern
  structs (a `CString` would outlive its call; an extern type its
  owner), aren't generic, declare nothing, and may have defaults and
  methods. Returning one, the wrapper gets a frozen zero value of the
  struct (a global made in init) for its shape.
- **Callbacks** are `R (*)(void *ctx, …)` and `void *ctx`, the context
  first in C's call (as `sqlite3_exec` and `pthread_create` have it).
  Their parameters are numbers, `bool`, `CString`(`?`) and `Pointer`(`?`),
  their results numbers, `bool`, `Pointer` or nothing; not `async`, not
  `throws`. **Lifetime**: the context lives for the extern call;
  `asuka_callback_retain` (`asuka_ffi.h`) keeps it until
  `asuka_callback_release`, from any thread. **Threads**: a call during
  the extern call, on its thread, runs at once and returns the result; any
  other is posted to the main thread, runs under the owner that made the
  callback (not at all once it's disposed), and returns zero. An error a
  callback raises during the call makes C's later calls of it return zero
  and the extern call throw it once C returns. A callback made in spawn
  can't be kept.
- **Names.** The builtins are `ffi.<import path>.<name>` (`main` for the
  root). The glue declares each C function from its declaration under a
  name of its own bound to the symbol (`__asm__`), so it includes no
  library header and clashes with none.
- **Registration survives static linking** as the app's module does: a
  core built with glue gets `-DAK_FFI`, and `ak_vm_builtins_init` calls
  `asuka_ffi_register`, which names it.
- **`[c]`** has `sources` (globs, `**` too), `include`, `defines`,
  `flags`, `libraries` (a name, or a path) and `frameworks` (Apple's),
  each platform's table adding to the top's; the app's covers its own
  packages, a dependency's its own. The app's C is compiled with its own
  flags and warnings (not the core's `-Werror`); the glue with the core's.
- **The player** is built with the C of the last compile; asuka dev
  rebuilds and relaunches it when a C file, `asuka.toml` or the glue (an
  extern declaration) changes, and keeps the new module for it.

## Milestones

Each ends with its tests green.

- [x] **F1** C from Asuka:
  - `extern fn`, `extern type`, `extern struct`, callbacks;
  - the glue's generator;
  - `[c]` in asuka.toml;
  - builds for every target (the player included);
  - `asuka test`;
  - the docs (language.md, a new c.md) and their translations.
- [ ] **G1** Desktop native packages in C:
  - [x] `asuka_desktop.h`, the registry (platform/desktop), its tests on a
        test host (no Go at run time);
  - [x] nativegen's C target, its golden files; the Go server target
        removed; the CLI builds packages' C into a C host's executable;
  - [x] asuka/host folded into asuka/platform;
  - [ ] the standard library's Windows and Linux code ported from Go to
        C: done for appearance, audio, biometric, camera, device, dialog,
        document, http, imageedit, js, location, maps, network,
        nowplaying, power, preview, print, purchase, record, screen,
        secure, sensors, shell, speech, sqlite, store, tracking,
        transcribe, watch, webauth (lottie and crash need none), with the
        barcode reader in C (platform/desktop's asuka_barcode.h, for scan
        and vision too) and one WinRT, COM and WinHTTP for the Windows
        host and the packages; left: bluetooth (and
        internal/bluetoothspec, internal/bluez), markdown (a parser in
        C), scan, vision, shader, svg; then package `native`'s servers,
        which only those packages' Go still uses.
- [x] **H1** The macOS host in Swift: every view, navigation, windows, menus, the
      tray, the host's modules; `asuka dev` and `asuka build` for the Mac
      without Go. platform/macos (its tests: platform/macos/mactest and
      `swift test --package-path platform/macos`); host/darwin and
      internal/darwin removed, with host/bridge, host/apphost,
      host/hosttest and cmd/asuka-host; asuka/watch's Swift for the Mac.
- [x] **H2** The Windows host in C, likewise:
  - platform/windows: C17 on Win32, the common controls v6, GDI+'s flat
    API, DWM, Direct3D 11, WebView2, UI Automation and WinRT through COM
    vtables; every view kind, navigation, core-virtualized lists, windows,
    the menu bar and its accelerators, flyouts, drag and drop, rich text,
    the host's modules (`update` among them), crash records, single
    instance, the window and route state;
  - platform/desktop: the C it shares with the Linux host;
  - `asuka build --platform windows` links the host, the core and the
    app compiled to C into one executable, with Zig from any system;
    `asuka dev`'s player is the host and the core, connecting with
    `asuka_dev_connect`;
  - `go test ./platform/windows`: apps built and driven in Windows (on
    Windows, or a VM); host/windows and internal/windows removed.
- [x] **H3** The Linux host in C, likewise:
  - platform/linux: C17 on GTK 4.14 and libadwaita 1.5, opened with
    dlopen (one set of declarations with packages' C, asuka_linux.h and
    asuka_linux.def); every view kind, navigation, lists, windows, menus,
    the tray (StatusNotifierItem, dbusmenu), flyouts, keys, drag and
    drop, AT-SPI, rich text, WebKitGTK, GStreamer video, GL shaders,
    portals, the host's modules (`update` among them), crash records,
    single instance, the window and route state;
  - platform/desktop: the C it shares with the Windows host;
  - `asuka build --platform linux` links the host, the core and the app
    compiled to C into one executable, in an install prefix, a .tar.gz
    and a Flatpak manifest, with Zig from any system; `asuka dev`'s
    player likewise without the app;
  - `go test ./platform/linux` under Xvfb: apps built and driven in real
    GTK windows; host/linux removed (internal/gtk and internal/bluez go
    with the standard library's Go desktop code, G1).
- [ ] **G2** The updater, crash records and the state files in the hosts;
      conformance and e2e on the new hosts.
- [ ] **X2** The Go runtime removed:
  - host/darwin, host/windows, host/linux, host/bridge, host/apphost,
    cmd/asuka-host;
  - package `native`;
  - the runtime's internal packages (darwin, windows, gtk, boot,
    update's client, winstate, routestate, flyout, animimage, barcode,
    rtf, bluez, bluetoothspec, crashrun, devstate, follow, appdir,
    localfile);
  - sdk/, ui, signal, app, uitest, colors, fonts.

  DESIGN.md, AGENTS.md and the docs describe the result.
