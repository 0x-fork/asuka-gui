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
| Desktop native packages | macOS: Swift (as now). Windows and Linux: C in the package (`windows/`, `linux/`), against generated glue | The platforms' own language. |
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

Shared C for Windows and Linux (Ed25519, SHA-512, the JSON the state
files need, the updater's staging) lives in platform/desktop.

## Native packages on the desktop

A package's Windows and Linux code is C:
- `windows/*.c` and `linux/*.c` in the package;
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

## Milestones

Each ends with its tests green.

- [ ] **F1** C from Asuka:
  - `extern fn`, `extern type`, `extern struct`, callbacks;
  - the glue's generator;
  - `[c]` in asuka.toml;
  - builds for every target (the player included);
  - `asuka test`;
  - the docs (language.md, a new c.md) and their translations.
- [ ] **G1** Desktop native packages in C:
  - `asuka_desktop.h`;
  - nativegen's C target;
  - the standard library's Windows and Linux code ported from Go to C.
- [ ] **H1** The macOS host in Swift: every view, navigation, windows, menus, the
      tray, the host's modules; `asuka dev` and `asuka build` for the Mac
      without Go.
- [ ] **H2** The Windows host in C, likewise.
- [ ] **H3** The Linux host in C, likewise.
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
