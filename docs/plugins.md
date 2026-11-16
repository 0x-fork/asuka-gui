# Plugins

A plugin is a package that brings native libraries with it: C, C++,
Zig or Rust, prebuilt for each platform and published once, which every
app that uses the package ships. A manifest beside the package's
`asuka.toml`, `asuka-plugin.toml`, names each library's file for each
platform and its SHA-256. `asuka/sqlite` is one
([Data](data.md#records-asukasqlite)): SQLite, built once with the same
options for every platform.

## Plugins, native packages and C

Asuka has three ways to bring code that isn't Asuka. A package can use
any of them together.

| | What it is | Built | Use it for |
|---|---|---|---|
| [C with `extern`](c.md) | C sources an `asuka.toml`'s `[c]` names, called from Asuka through `extern fn` | with the app, for each target, by the CLI's compilers | your own C, a small library's sources, C you want inlined into the app |
| [A native package](native.md) | a `native interface` or `native component` implemented in Swift, Kotlin, JavaScript and C | with the app, from each platform's project (a Swift package, a Gradle module, `web/`, `desktop/`) | the platforms' own APIs and views |
| A plugin | libraries prebuilt per platform, named in `asuka-plugin.toml` | once, by the plugin's own build, then published | a large library, one with a toolchain of its own (Rust, a configure step), one that must be the same build on every platform |

A plugin's libraries aren't compiled into the app: each is a library
of its own (a `.dylib`, a `.so`, a `.dll`, an iOS framework, a
WebAssembly module), loaded at run time by the package's code. Building
an app never builds them, so an app that uses SQLite doesn't compile
SQLite.

## Using a plugin

Import its package:

```asuka
import asuka/sqlite

fn openNotes() throws -> sqlite.DB = try sqlite.open("notes.db")
```

Nothing else. `asuka build` finds the plugins among the app's packages
(the app's own directory, its dependencies, the standard library
packages it imports) and ships each library's file for the build's
platform. `asuka dev`, `asuka test` and `asuka run` use the plugin's own
build when it's a checkout with one, or else the published file,
downloaded the first time into the user's cache (`asuka/plugins` in the
system's cache directory) and used from there after. Every downloaded
file is checked against the SHA-256 the manifest names before it's
used. A package from the registry doesn't carry the libraries: its
manifest says where they are.

### Where the libraries go

| Platform | Where | |
|---|---|---|
| macOS | `Contents/Frameworks` | signed with the app |
| Windows | beside the `.exe` | signed with it, with `ASUKA_WINDOWS_CERT` |
| Linux | `lib/<app>` of the install prefix | in the tarball and the Flatpak |
| iOS | `Frameworks/<library>.framework` | its Info.plist and privacy manifest, signed with the app |
| Android | beside `libasuka.so`, in each ABI | |
| The web | `lib/` of the host's files | the page's `libs` option names each file's URL |

The libraries of an app's plugins all go in one place, so no two may
have a file of the same name for a platform: the build says which two do.

While developing, `asuka dev` puts them where the player finds them: on
a desktop it points the core at each file (`ASUKA_LIB_<NAME>`, the
library's name in capitals: `ASUKA_LIB_ASQLITE`), on Android it pushes
them beside the player's core, on iOS they're frameworks in the
player, and on the web it serves them under the page's `asuka/lib/`.
`asuka test` and `asuka run` set `ASUKA_LIB_<NAME>` too.

An app built into another (`asuka build --platform ios --xcframework`)
comes with an XCFramework of each library, for that app to embed too.

## Making a plugin

A plugin is a package ([Packages](packages.md)) with an
`asuka-plugin.toml` beside its `asuka.toml`, a build that makes its
libraries, and code that loads them.

### The manifest

```toml
name = "sqlite"
sources = ["csrc"]

[[libraries]]
name = "asqlite"
version = "3.54.1"
release = "https://github.com/you/sqlite-plugin/releases/download/plugin-sqlite"

[libraries.files]
darwin = {name = "libasqlite.dylib"}
ios = {name = "asqlite"}
ios-simulator = {name = "asqlite"}
android-arm64 = {name = "libasqlite.so"}
android-arm = {name = "libasqlite.so"}
android-amd64 = {name = "libasqlite.so"}
linux-amd64 = {name = "libasqlite.so"}
linux-arm64 = {name = "libasqlite.so"}
windows-amd64 = {name = "asqlite.dll"}
windows-arm64 = {name = "asqlite.dll"}
wasm = {name = "asqlite.wasm"}
```

- `name` is the plugin's, as the CLI's messages and its release name it:
  letters, digits, `-`, `_` and `.`, not first.
- `sources` are what its libraries are built from, relative to the
  manifest (a directory is its files, whole): a library built from
  older sources isn't published. Without it, every file of the package
  but `build/`, the manifest, `testdata` and dotfiles.
- Each library's `name` is how its code asks for it
  (`ASUKA_LIB_ASQLITE`), and its iOS framework's name. `version` is its
  version, for that framework's Info.plist. `release` is the GitHub
  release its files are published to:
  `https://github.com/<owner>/<repo>/releases/download/<tag>`.
- `files` name its file for each platform: `darwin` (one universal file
  for both Macs, arm64 and x86_64), `ios` and `ios-simulator`,
  `android-arm64`, `android-arm`, `android-amd64`, `linux-amd64`,
  `linux-arm64`, `windows-amd64`, `windows-arm64` and `wasm`. A build
  for a platform the library has no file for fails, saying so. A Mac's
  is a `.dylib`; an iOS one is named for the library (its framework's
  binary, with `@rpath/<library>.framework/<library>` as its install
  name).

`asuka plugin publish` adds what it published to each file's entry
(`asset`, `size`, `sha256`, `rawSize`, `rawSHA256`, `source`): don't
write those yourself.

### Building

A plugin builds its libraries with a tool of its own, as they need: the
sqlite plugin compiles SQLite and its entry point with Xcode for the
Mac and iOS, the Android NDK, and Zig for Windows, Linux and the web. A
shell script, a Makefile or a program will do. For each target it
writes the library to `build/<target>/<file>` beside the manifest, and
beside it `<file>.source`, which holds the hash of the sources it was
built from: the SHA-256 of the `sources`' files in order of their
paths (relative to the manifest, with `/`), each as its path, a space,
its size in bytes and a newline, then its bytes.

`build/` isn't checked in, and a package's tarball never carries it. In
a checkout, `asuka dev`, `asuka test` and `asuka run` use the build
there before any published file, so the plugin's own tests and the apps
beside it run what you just built.

### Loading the library

The package's code loads the library with the platform's loader
(`dlopen`, `LoadLibrary`, WebAssembly's instantiation) and looks up its
functions, so the app needs no link-time dependency on it. It looks, in
order:

1. In `ASUKA_LIB_<NAME>`, the file's path, where `asuka dev`, `asuka
   test` and `asuka run` point it.
2. Where `asuka build` put it, from the executable's directory:
   `../Frameworks/<file>` on a Mac (its `Contents/MacOS`), the `.exe`'s
   directory on Windows, `../lib/<exe>/<file>` on Linux (its `bin`),
   `Frameworks/<library>.framework/<library>` on iOS (and
   `../../Frameworks/…` from an app extension's).
3. By name, where the system's loader looks: `lib<library>.so` on
   Android (the APK's `lib/<abi>`), `@rpath/<library>.framework/<library>`
   on iOS.

On the web, the page's `libs` option maps each file's name to its URL.

`asuka/sqlite`'s is the core's own C ([core/src/sqlite](../core/src/sqlite/sqlite.c)),
which loads `asqlite` the first time a database opens and runs it
off the main thread; on the web, SQLite's worker
([platform/web/src/sqlite-worker.js](../platform/web/src/sqlite-worker.js))
instantiates `asqlite.wasm`. A package of yours loads its library from
its [native code](native.md): its desktop C, Swift and Kotlin, and
JavaScript on the web.

Keep a library's exported symbols few, its own and nothing it links:
another copy of what it's built from may be in the process (a system
SQLite that GTK loads). Call it a whole operation at a time, not a
function per value: the sqlite plugin has one entry point,
`asq_call`, that runs a whole statement and returns its rows in one
buffer, which is also what lets the web run it in a worker.

### Publishing

```sh
asuka plugin publish path/to/plugin     # the directory with asuka-plugin.toml (default: .)
```

`asuka plugin publish` gzips each library's build and names it for its
hash (`asqlite-darwin-1982f596dd7c-libasqlite.dylib.gz`). It uploads
the ones the release doesn't have yet, with `gh`, making the release if
it isn't there. It downloads each one back to check it, and records
them in the manifest, which is what you commit. It writes the whole
manifest, under a comment that says so, so comments of yours in it
don't last. A file a release has is never replaced, so every manifest
a commit had keeps working. A build older than the plugin's sources
stops it: build again first.

Then publish the package as any other (`asuka pkg publish`,
[Packages](packages.md)): its tarball carries the manifest, and the
apps that depend on it download the libraries from the release, for the
platforms they build for.
