# Getting started

Apps are written in the Asuka language ([the language](language.md)) and
run on Asuka's core, a C library, with each platform's own widgets. The
`asuka` CLI compiles them and builds the core for each platform with
that platform's own compiler; one command makes an app, and another
runs it, reloading as you save.

## What you need

The CLI, and for each platform you run on, the tools that build for it:

- **iOS**: a Mac with Xcode, and its iOS simulator or an iPhone.
- **Android**: a Mac, a Windows 10 or 11 PC (x64 or Arm) or Linux, with
  Android Studio and, from its SDK Manager, an NDK (SDK Tools → NDK),
  and an emulator from its Device Manager, or a phone with USB debugging
  on. Asuka finds the SDK in its usual place, or at `ANDROID_HOME`, and
  Android Studio's Java; it builds the core with the NDK's clang, and the
  app with Gradle. On an Arm PC, the NDK and the emulator run emulated
  (they're x64), and the emulator needs an x86_64 system image.
- **macOS**: a Mac, macOS 11 or later, with Xcode's command line tools
  (`xcode-select --install`; Xcode includes them): their clang builds
  the core.
- **Windows**: a PC with Windows 10 or 11, x64 or Arm, with LLVM's clang
  (`winget install LLVM.LLVM`), or [Zig](https://ziglang.org/download)
  with `ASUKA_CC=zig`. `WebView` and `Video` use the Microsoft Edge
  WebView2 Runtime, which Windows 11 has, as most Windows 10 PCs do.
- **Linux**: a C compiler (`build-essential`, `gcc`, `clang`), and GTK
  4.14 and libadwaita 1.5 or later (Ubuntu 24.04, Fedora 40, Debian 13
  and later have them), on Wayland or X11:

  ```sh
  sudo apt install build-essential libgtk-4-1 libadwaita-1-0   # Debian, Ubuntu
  sudo dnf install gcc gtk4 libadwaita                         # Fedora
  sudo pacman -S base-devel gtk4 libadwaita                    # Arch
  ```

  `WebView` needs WebKitGTK 6.0 (`libwebkitgtk-6.0-4`), and `Video` and
  audio GStreamer; a package that needs a library that isn't there says
  which, as an error.
- **The web**: a browser, and [Zig](https://ziglang.org/download), which
  builds the core as WebAssembly.

With Zig installed, `asuka build` also makes a Windows or Linux app from
any system (the core cross-compiled by `zig cc`), and a Mac app from a
Mac.

## Install the CLI

```sh
npm install -g @asuka/cli     # or: npx @asuka/cli new hello
```

The package has a prebuilt `asuka` for macOS, Windows and Linux (x64 and
Arm); nothing is built on install. From a checkout of the repository,
`go run ./cmd/asuka` runs the CLI from source (Go 1.26 or later).

## Make an app

```sh
asuka new hello
cd hello
```

`hello` has:

- `app.asuka`, the app: a component, the `app` block that shows it, and a
  test.
- `asuka.toml`, its name, its ID (the bundle ID, application ID and
  desktop app ID) and version, and what [the stores and each
  platform](shipping.md) need.
- `.gitignore`.

## Run it

```sh
asuka dev                      # this computer: on a Mac, a PC or Linux
asuka dev --platform ios        # the iOS simulator
asuka dev --platform ios --device iphone  # the iPhone plugged in
asuka dev --platform android    # an Android emulator or phone
asuka dev --platform web        # your browser
```

Save an `.asuka` file and the app reloads in a few milliseconds, its
state kept. Its output and its errors are in the terminal,
with each reload's report (`asuka: reloaded in 2.1 ms, 3 states kept`).

`asuka dev` builds the platform's **player** once: the app's shell (its
icon, its name, its permissions) with Asuka's core, but none of the
app's code. It installs and launches it, and the player connects back to
`asuka dev`, which compiles the app to bytecode and sends it; on each
save it compiles again and sends the new module, which the player loads
in place. A save that doesn't compile prints its errors, and the app
keeps running what it had. Changing `asuka.toml`, the assets or native
code builds and launches the player again.

- **On your computer** (`asuka dev`, or `--platform mac`, `windows`,
  `linux`), the player runs in a window.
- **On a phone**, `--device` picks the simulator or the iPhone (its name
  or UDID, or `iphone` for the one plugged in) or the Android device (an
  adb serial or an AVD's name). `--once` launches the app without
  watching.
- **In a browser**, Asuka serves the app at `http://localhost:8790/` and
  opens it; each save's module loads into the page in place. `--lan`
  serves it to your network, to try it in a phone's browser. See [the
  web](web.md).

To run on an iPhone, plug it in, unlock it, tap Trust and turn on
Developer Mode (Settings › Privacy & Security), and add your Apple ID to
Xcode (Settings › Accounts): `asuka dev --platform ios --device iphone`
signs the player with your team and installs it, the first time; the
iPhone reaches `asuka dev` over your network. See
[Devices](development.md#devices).

[Development](development.md) has the rest: what a reload keeps,
devices, logs, and the problems you may meet.

## Change it

`app.asuka` from `asuka new` is a counter:

```asuka
component Counter {
  state count = 0

  SafeArea {
    justify: .center
    align: .center
    gap: 16

    Text("hello: {count}") { font: .title }
    Button("Add") {
      variant: .filled
      on press { count += 1 }
    }
  }
}

app { Counter() }

test "Add counts" {
  let app = mount(Counter())
  app.tap("Add")
  app.expect("hello: 1")
}
```

- **State** is `state count = 0`: reading it in a view (`"{count}"`)
  binds the view to it, and writing it (`count += 1`) updates what read
  it, and only that.
- **Components** declare their state, props and views; their body isn't
  run again when something changes, only the bindings that read it.
- **Style** is flexbox, in points: `grow`, `gap`, `padding`, `align`,
  `justify`, and the look (`background`, `radius`, `shadow`...). An app
  that sets none looks as the platform's apps do.
- **Tests** are `test` blocks: `asuka test` runs them on a fake host that
  lays the views out as a phone would (see [Testing](testing.md)).

The same code is the app on every platform. [The language](language.md)
has the rest of it.

## An app's files

- `assets/`: images and other files, `Image("photo.jpg")`, and fonts in
  `assets/fonts`.
- `locales/`: the app's messages in each language (see
  [Languages](i18n.md)).
- Packages of its own: a directory of `.asuka` files beside the app's,
  `import ./components`. Dependencies are in `asuka.toml`'s
  `[dependencies]` (`asuka add @acme/charts`; see
  [Packages](packages.md)).
- Native packages: Asuka packages that declare a `native interface` or a
  `native component`, which each platform implements in its own project
  (a Swift package, a Gradle module, JavaScript, Go on the desktop). See
  [Native code](native.md).

## Next

- [Development](development.md): the dev loop, devices, logs.
- [Testing](testing.md): `test` blocks and `asuka test`.
- [Shipping](shipping.md): builds for the App Store, Google Play, the
  Mac, Windows, Linux and the web.
