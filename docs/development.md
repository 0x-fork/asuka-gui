# Development

`asuka dev` runs an app and reloads it as you save: on the iOS simulator
or an iPhone, an Android emulator or phone, your Mac, PC or Linux
machine, or your browser. This page covers how it does that on each,
what a reload keeps, devices, assets and logs, editors, the problems
you may meet, and Asuka's own tests and benchmarks.

## asuka dev

```sh
asuka dev [flags] [dir]                     # this computer: macOS, Windows or Linux
asuka dev --platform ios [flags] [dir]       # the iOS simulator
asuka dev --platform ios --device iphone [dir]  # the iPhone (or iPad) plugged in
asuka dev --platform android [flags] [dir]   # an Android emulator or phone
asuka dev --platform web [flags] [dir]       # your browser
```

`--platform` is one of `ios`, `android`, `mac`, `windows`, `linux` and
`web`; `asuka build` takes it likewise. Without it, the app runs (or
builds) for the computer you're on. One platform per run: run two
terminals for two.

`dir` is the app's directory, the current one by default: its `.asuka`
files, one of which has the `app` block, and its `asuka.toml`. The
flags:

- `--platform`: as above.
- `--lang`: show a language for this run (`de`, `zh-Hans`), or a pseudo
  locale: `en-XA` (accented and longer) or `ar-XB` (right to left).
  `--dir rtl` lays any app out right to left. See
  [Languages](i18n.md).
- `--once`: launch the app once, without watching for changes.
- `-v`: print the commands Asuka runs.

On a phone:

- `--device`: on iOS, a simulator's name or UDID (by default the booted
  iPhone, else a recent iPhone Pro, which it boots), a connected iPhone's
  or iPad's name or UDID, or `iphone` for the one connected (see
  [Devices](#devices)); on Android, an adb serial or an AVD's name (by
  default the first device adb lists, else the first AVD, which it
  starts).
- `--team`: on an iPhone, the Apple Developer team that signs the
  player.

In a browser: `--port`, `--lan` (serve to your network, for a phone's
browser) and `--no-open`; see [the
web](web.md#asuka-dev---platform-web).

## How it works: the player

An app runs on Asuka's core, a C library: a release has the app compiled
to C and built into it; in development the core runs the app's
**bytecode** on its VM instead, and can swap it for the next build's
while the app runs.

`asuka dev` builds the platform's **player** once: the app's shell (its
name, ID, icon, `Info.plist` or manifest, its native packages' code)
with the core and no app in it. It installs and launches it, and the
player connects back to `asuka dev`, which compiles the app and sends
it the module. On each save, `asuka dev` compiles again (a few
milliseconds) and sends the new module; the player loads it in place,
keeping the app's state, and reports how it went:

```
asuka: compiled in 3.1 ms
asuka: reloaded in 2.2 ms, 4 states kept
```

A save that doesn't compile prints its errors, as `file:line:col:
message`, and the player keeps running what it had; the next save that
compiles goes on from there. A change to the rest of the app (its
`asuka.toml`, its assets, its native packages' Swift, Kotlin or
JavaScript) builds the player again, reinstalls it if it changed, and
launches it.

What each platform's player is:

- **macOS**: the Swift host linked with the core (and the app's native
  packages' Swift), in an `.app` like a release's, in the app's
  directory in Asuka's cache; launched with `ASUKA_DEV=host:port` in its
  environment.
- **Windows, Linux**: one program, the C host (on Win32; on GTK 4 and
  libadwaita) linked with the core and no app, in the app's directory
  in Asuka's cache (on Windows a console program). It's launched with
  `ASUKA_DEV=host:port` in its environment.
- **iOS**: the Swift host linked with the core (`libasuka.a`) into the
  app's `.app`, with every part of the host that links a framework of
  its own (WebKit, AVKit...), so the app may use any of them as you edit
  it. `xcrun simctl` installs and launches it on the simulator, with
  `ASUKA_DEV` in its environment; on an iPhone, it's signed for
  development and installed with `xcrun devicectl`.
- **Android**: the debug shell app (Gradle builds it once, and again
  when its manifest, Kotlin or resources change), with the core pushed
  into its private files (`files/asuka/libasuka.so`), which the debug
  app loads from; `files/asuka/dev` says where `asuka dev` is, and `adb
  reverse` makes it the device's own loopback.
- **The web**: the page loads the core as WebAssembly (`asuka.wasm`)
  and the app's module from the dev server, and each new one as it's
  built.

## On the desktop

On macOS, Windows and Linux, `asuka dev` runs the player in a window.
The first run builds the player for this computer, which takes a
moment; later runs start at once. It needs a C compiler: Xcode's command
line tools on a Mac; on Windows and Linux Zig, clang or gcc (`ASUKA_CC`
picks one).

The app reads its `assets/` where they are, so changing one needs no
copy. Its data (SQLite databases, files) is in a `data` directory of its
own in Asuka's cache (see [Starting over](#common-problems)), not the
installed app's, so `asuka dev` and a release don't share it. Its output
and its errors print in the terminal.

**macOS.** The player is an `.app` like a release's, in Asuka's cache:
its `Info.plist` (its name, bundle ID, URL scheme, purpose strings and
languages from `asuka.toml`) and icon, signed ad hoc. So the app is
itself to the system, as when it's opened from the Finder: the Dock
shows its name and icon, notifications are its own, and its privacy
prompts (the camera, location, contacts) ask for it, with its purpose
strings, not for the terminal. Packages that need a provisioning
profile's entitlements (push, Sign in with Apple, App Attest) work only
in a release build signed with one: see [Shipping](shipping.md#macos).

**Windows.** `asuka dev` runs only on Windows itself; `asuka build
--platform windows` builds from any system (with Zig), one `.exe` with
the app in it. The player is a console program, so its output prints in
the terminal; a release has no console. To stop it, Asuka closes its standard input (Windows can't send
a process Ctrl-C).

**Linux.** `asuka dev` on Linux runs the player on GTK 4 and libadwaita,
which it needs installed (see [Getting
started](getting-started.md#what-you-need)), under your session's
display, Wayland or X11. On a Mac or a PC, `asuka dev --platform linux`
runs it only where `ASUKA_LINUX_RUN` names a command that runs a program
on a Linux machine with a display, which sees this computer's files at
the same paths: a VM, as `ASUKA_LINUX_RUN="orb -m ubuntu"` (OrbStack) or
`ASUKA_LINUX_RUN="limactl shell default"` (Lima). Asuka builds the
player for that machine's architecture (the core with Zig), and the
player reaches `asuka dev` over the network: at this computer's first
address, or `ASUKA_DEV_HOST`'s.

## What a reload keeps

A reload keeps every `state` of the app, with no code of yours, on every
platform: each state cell is kept under its component's place in the
view tree and its name (`Inbox.query`, under the screen and the row
that show it), and the new module's cell at the same place, with the
same name, starts from its value. Values are converted to the new
module's types by their fields' and cases' names: a field added starts
from its default, one removed is dropped. A state whose type changed in
a way that can't be converted (an `int` that's now a `string`) starts
again from its initial value. The views are made again from the new
module, showing the state they had: the screen looks as it did, with
the change.

What isn't kept:

- A state renamed, or moved to another component, starts over; so does
  one whose component moved to another place in the view tree.
- `let` values aren't kept: they're computed again from what they read.
- Tasks and effects start again in the new module (a `task` running
  when you save is cancelled, and the new one runs).

Stopping `asuka dev` and running it again starts the app from nothing.
Files the app writes (SQLite databases, files) are kept: they're in the
app's data directory, on the device or, on the desktop, in Asuka's
cache.

## Reloading

Asuka watches the app's directory and its local dependencies'
(`file:` in `asuka.toml`), as the system says they change (FSEvents,
inotify, ReadDirectoryChangesW; where it can't, by polling):

- `.asuka` files: compiled and sent to the player;
- `asuka.toml` and `asuka.lock`, files under `assets/`, the language
  catalogs, native packages' Swift, Kotlin and JavaScript, `Info.plist`
  and entitlements: the player is built and launched again (the app
  starts from nothing).

It skips directories starting with `.`, and `testdata`, `build`, `dist`
and `node_modules`.

## Devices

**Your computer**: `asuka dev` alone, on a Mac, a PC or Linux.

**The iOS simulator** shares the Mac's network, so the player reaches
`asuka dev` on its loopback.

**An Android emulator or phone**: `adb reverse` gives the device the
port `asuka dev` listens on, on its own loopback, for an emulator and a
phone over USB alike. For a phone, turn on USB debugging in its
developer options, plug it in and accept the prompt, and check that
`adb devices` lists it. `asuka dev --platform android` takes the first
device adb lists, phone or emulator; with several, name it with
`--device` and its serial.

**An iPhone** (or iPad), with Xcode 15 or later: plug it in, unlock it
and tap Trust; turn on Developer Mode in its Settings › Privacy &
Security (it shows once the phone has met Xcode; the phone restarts).
Then `asuka dev --platform ios --device iphone` runs on it, or
`--device` with its name or UDID when several are connected (`xcrun
devicectl list devices` lists them).

- **Signing**: the player is signed for development by your team:
  `--team`, else `ASUKA_TEAM_ID`, else `asuka.toml`'s `ios.team`, else
  the team of the Apple Development certificate in your keychain. The
  first run has Xcode make the development profiles it needs
  (`xcodebuild -allowProvisioningUpdates`), registering the phone with
  the team and turning on the capabilities the app uses (push, App
  Groups...), with the Apple ID in Xcode › Settings › Accounts, or with
  an [App Store Connect API key](shipping.md) if `ASUKA_ASC_KEY_ID` (or
  `ios.appStoreConnect`) sets one. A team's profiles that already fit,
  wildcard ones too, are used as they are.
- **Reaching your Mac**: plugged in, the Mac connects to the player
  through the cable (usbmuxd, as Xcode does), and the phone needn't be
  on any network. Unplugged, the player connects to `asuka dev` over the
  network, trying each of the Mac's addresses (or `ASUKA_DEV_HOST`), on
  port 8791 when it's free; iOS asks once whether the app may find
  devices on it (allow it; its Settings › Apps › Local Network turns it
  on later). Either way it reconnects by itself when `asuka dev` comes
  back, and a player opened again from the home screen finds it. If the
  player doesn't connect, Asuka says what to check.

**A phone's browser**: `asuka dev --platform web --lan` serves the app to
your network, and prints the addresses to open.

## Assets

On the desktop, the player reads `assets/` in place. On a phone, they're
in the player: copied into the `.app` on iOS, pushed into the app's
private files on Android, when they changed. In the browser, the dev
server serves them. A change to an asset builds and launches the player
again.

## Logs

The player sends its output and its errors to `asuka dev`, which prints
them in the terminal: `print`, uncaught errors, and what the core and
the host write, on every platform. A module the player couldn't load
says why (`asuka: the player: ...`). In the browser, the page's errors
print there too.

## Editors

`asuka lsp` is Asuka's language server: diagnostics as you type,
completion, hover with `///` docs, go to definition, references,
rename, formatting (`asuka fmt`'s), symbols, signature help and
semantic highlighting for `.asuka` files. It checks the package being
edited with your unsaved changes, and the packages that import it.

For VS Code, the extension is in `editors/vscode`: it adds the
language's highlighting and runs `asuka lsp`, the `asuka` on your `PATH`
or the one `asuka.server.path` names.

```sh
cd editors/vscode
npm install
npx @vscode/vsce package
code --install-extension asuka-0.1.0.vsix
```

Any editor with an LSP client can use it too: run `asuka lsp`, which
speaks LSP 3.17 over standard input and output, for `.asuka` files,
with the directory of `asuka.toml` as the root. `editors/vscode`'s
README has Neovim's and Helix's settings.

## Snapshots and scripts

The desktop hosts take a few environment variables that make the app
show a state and capture it, for checking a change by eye or in CI. Run
a release build's executable with them, or `asuka dev` (which passes its
environment to the player):

```sh
ASUKA_SNAPSHOT=shot.png ./MyApp.app/Contents/MacOS/MyApp            # the window as a PNG, once it has shown, then quit
ASUKA_SNAPSHOT=shot.png ASUKA_DUMP=1 asuka dev                      # and its view tree, in shot.png.txt
ASUKA_SCRIPT="tap:Add;tap:Add" ASUKA_SNAPSHOT=shot.png asuka dev    # steps first
```

- `ASUKA_SNAPSHOT` writes the window as a PNG once it has shown (and
  the script has run), and quits; `ASUKA_SNAPSHOT_STAY=1` keeps the app
  running. On macOS it's the window server's image of the window, with
  its sheets over it, so materials and Liquid Glass show as they do on
  screen; a file named `*.content.png` is the content alone, without the
  title bar.
- `ASUKA_DUMP` writes the view tree beside it: each view's kind, frame
  and what it shows.
- `ASUKA_SCRIPT` runs steps, separated by `;`, before the snapshot:
  `tap:<text>` (the view showing it: a button's title, a text, a tab, a
  sidebar's row, a segment), `type:<text>` (into the focused field),
  `back`, `key:<shortcut>` (`ctrl+shift+k`), `menu:<item>` (the menu
  bar's), `escape`, `close` (the window in use), `wait:<duration>`
  (`500ms`), `waitfor:<file>` (until the file exists: a test says when
  to go on) and `snap:<file>`. Each host has a few more: see its
  automation (`platform/macos/Sources/AsukaHost/Automation.swift`,
  `platform/windows/src/automation.c`, `platform/linux/src/automation.c`).

## The build cache

What builds compile and download is kept in Asuka's cache and shared by
every app on your computer: a second app, or a build after a small
change, compiles only what's new. The first build for a platform
downloads Asuka's prebuilt libraries for it, once, from the npm
registry: the registry and token in your `.npmrc` (or
`npm_config_registry`) apply, for a mirror or a proxy. Its Swift
libraries are used with the Xcode they were built with or a newer one;
with an older Xcode, they're compiled once on your computer instead.

- `asuka cache stats` prints where the cache is, its size, and how often
  builds found what they needed in it.
- `asuka cache clean` empties it; the next builds compile everything
  again.
- The cache stays under 10 GB, removing what was used least lately.
  `ASUKA_CACHE_MAX` sets another limit (`ASUKA_CACHE_MAX=20G`, `500M`;
  `0` for none), and `ASUKA_CACHE` another place for it.

## Common problems

- **`cbuild: no C compiler`**: install your system's (Xcode's command
  line tools, `build-essential`, LLVM), or Zig with `ASUKA_CC=zig`.
  `building for the web needs Zig` and `cross-compiling for ... needs
  Zig`: install [Zig](https://ziglang.org/download), which builds the
  core as WebAssembly and for other systems.
- **`no iOS simulator SDK (install Xcode)`**: install Xcode, open it
  once, and add an iOS simulator runtime in its Components settings.
  `no iPhone simulator; add one in Xcode` and `no simulator "..."`
  mean there's no simulator of that name: `xcrun simctl list devices`
  lists them.
- **An iPhone that isn't ready**: `... isn't paired with this Mac`
  (plug it in, unlock it and tap Trust), `... isn't reachable` (plug it
  in, or wake it on the Mac's Wi-Fi), `... has Developer Mode off`
  (Settings › Privacy & Security › Developer Mode), `... is locked`
  (unlock it). `no iPhone or iPad is connected` with one plugged in:
  check that `xcrun devicectl list devices` lists it.
- **`Xcode couldn't make the development profiles`**: sign in to Xcode
  with the team's Apple ID (Xcode › Settings › Accounts), or set an App
  Store Connect API key; a bundle ID another team has needs another
  `id` in `asuka.toml`. `the keychain has Apple Development certificates
  of N teams`: pick one with `--team`.
- **`the player hasn't connected yet`**: plug the phone in and unlock
  it, or put it on the Mac's network, allow its Local Network access,
  and let `asuka` take connections in the Mac's firewall (System
  Settings › Network › Firewall).
- **`no Android SDK: install Android Studio or set ANDROID_HOME`**: Asuka
  looks in `ANDROID_HOME`, `ANDROID_SDK_ROOT`, then
  `~/Library/Android/sdk` on a Mac, `%LOCALAPPDATA%\Android\Sdk` on
  Windows and `~/Android/Sdk` on Linux.
- **`no Android NDK`**: install one from Android Studio's SDK Manager
  (SDK Tools, NDK). Asuka takes the newest that has a build for your
  computer (`windows-x86_64` on any Windows PC, Arm too).
- **`no Android device or emulator`**: make an AVD in Android Studio's
  Device Manager, or plug in a phone. Asuka waits up to four minutes for
  an emulator it starts to boot (`the emulator didn't boot`).
- **Java**: Gradle needs a JDK: `JAVA_HOME`, else Android Studio's own,
  where its installer or JetBrains Toolbox put it (on Windows, in
  Program Files or `%LOCALAPPDATA%\Programs`).
- **The first Android build is slow**: Gradle downloads itself and
  Android's libraries, once.
- **A release build on the phone**: an app installed by `asuka build`
  (another signing key) is uninstalled, with its data, to install the
  player.
- **`asuka dev --platform windows runs the app on this computer`**:
  `asuka dev` runs a Windows app on Windows only. Build it elsewhere
  with `asuka build --platform windows`, and copy the folder to a PC.
- **`... isn't installed` on Linux**: the host needs GTK 4.14 and
  libadwaita 1.5 or later, and some packages a library of their own:
  WebKitGTK 6.0 for `WebView`, GStreamer for video and audio, libsecret
  for secure storage. The error names it; install it with your
  distribution's package manager.
- **A Linux app that doesn't open**: it needs a display. Over SSH or in
  CI, run it under Xvfb (`xvfb-run -a`), with a session bus
  (`dbus-run-session`) for what goes through D-Bus.
- **Starting over**: Asuka's builds, caches and the apps' development
  data are in Asuka's cache directory: `~/Library/Caches/asuka` on a
  Mac, `~/.cache/asuka` on Linux, `%LocalAppData%\asuka` on Windows.
  `asuka cache clean` empties it, to build everything again, with no
  data.

## Tests

To test your app's interface with `asuka test`, with no device or
display, see [Testing](testing.md). Asuka's own repository tests the
core, the CLI and each host through the platform's real views.

```sh
go test ./core                                     # the core's C tests (ASan, UBSan) and the VM's
go test ./internal/appbuild ./cmd/asuka            # the builds and the dev loop, on every platform this computer has
go test ./platform/macos/mactest                   # macOS: apps on the Mac player, its real AppKit views
go test ./platform/windows                         # Windows: apps built with Zig, driven in real windows (on Windows, or a VM)
xvfb-run -a dbus-run-session go test ./platform/linux  # Linux: apps driven in real GTK windows
swift test --package-path platform/ios             # the Swift host's decoder
swift test --package-path platform/macos           # the Mac host's Swift and its decoder
node --test platform/web                           # the web host's decoder
```

`go test ./cmd/asuka` runs the dev loop end to end where it can: on this
Mac (the player, a module, a tap, the next module with the count kept),
in headless Chrome, on the booted iOS simulator and on a running Android
emulator, and builds the app for each platform; it skips what this
computer can't build or run.

`go test ./platform/macos/mactest -run Conformance` checks what the
host draws for a style (edges, corners, borders, shadows, gradients),
measured in a capture of the window; the e2e tests do the same on the
simulator, the emulator and headless Chrome (`go test -tags e2e ./e2e
-run Conformance`). On Windows, `go test ./platform/windows -run
Drawing` checks images, shapes, canvases, shaders, rounded clips and
materials by their pixels in the window's capture.

`go test ./platform/windows` runs each test's app in Windows: on a PC,
or from a Mac in the swiftvm VM that `ASUKA_WINDOWS_VM` names
(`Windows Fresh` by default), which sees `~/Library/Caches/asuka/windows-vm`
as `Z:\windows-vm`; with neither, they skip.

## Benchmarks

`bench/` compares Asuka with SwiftUI and Jetpack Compose: the same app
written in each, timed frame by frame in release builds.

```sh
go run ./bench/run -ios        # asuka and SwiftUI on the simulator
go run ./bench/run -android    # asuka and Compose on the running emulator or device
```

`-runs` sets how many runs of each (5 by default); the medians go to
`bench/results`. [`bench/README.md`](../bench/README.md) has the
scenarios, how they're measured, and the latest results.

## asuka.toml

`asuka dev` and `asuka build` read the same `asuka.toml`; see
[the asuka.toml reference](asuka-toml.md) for its keys, and
[Shipping](shipping.md#asukatoml) for how they come together for a
release.
