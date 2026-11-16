# Asuka — design

Asuka is a UI framework that renders with each platform's own widgets,
driven by fine-grained signals. **Mobile comes first: iOS and
Android**; macOS, Windows, Linux and the web run on the same core. Apps
are written in **Asuka**, a language of its own (docs/language.md), and
run on a **core written in C** (core/). RFC 0007 has the reasoning for
both. The goals, in order:

1. **Fast to build with**: a save shows on the device in milliseconds,
   with its state and its views kept; a small, checked language; one
   command for every platform.
2. **Best performance**: an update touches only the native widgets it
   changes, crosses into native code once per batch, and allocates
   nothing in the steady state; release builds are compiled to C.

Non-goals: drawing widgets ourselves (the one exception is rich text,
see "Rich text"), web views (the browser is a platform of its own, see
"The web"), pixel-identical looks across platforms, stability across a
large audience before 1.0.

## Decisions

| Question | Decision | Why |
|---|---|---|
| Platforms, in order | iOS, Android; then macOS, Windows, Linux, the web | The core and `ops` are platform-neutral. |
| App language | Asuka: statically typed, component bodies declarative, async/await, value semantics (docs/language.md) | Reactivity the compiler checks (a read that should follow can't be read once), main-thread rules as type errors, and code a VM can reload without restarting. |
| Core language | C17 (core/) | Every platform's toolchain and FFI calls C; no runtime or GC of its own on the phone; a small WebAssembly build. |
| Compiler | Go (lang/), part of the CLI | The device never compiles; the CLI and the language server share it. |
| Running the app | Bytecode on the core's VM in development; C compiled with the core for release (lang/cgen) | Reloads keep state and views on every platform, physical phones included; releases run at native speed. |
| Memory | Reference counting, copy on write; owners dispose explicitly | No GC pauses; values can't make cycles. |
| Retained or immediate | Retained | Native widgets are retained objects that own focus, selection, IME, scroll and accessibility state. |
| Reactivity | Fine-grained signals (alien-signals' algorithm, Solid's owners), no virtual tree | A write re-runs only what read it, and each binding sends only the prop it sets. |
| Threads | The core runs on the platform's main thread; `spawn` runs values-only work on workers | Native callbacks that need answers synchronously (list data sources, text field delegates, measurement) call straight into the core. |
| Core ↔ native contract | Batched mount operations (`ops`), encoded once per batch | One crossing per batch, the same stream in process and over a socket. |
| Layout | Our own engine (core/src/layout): CSS block, flexbox and grid, checked against Chrome, measured by the host | One layout for every platform, with the semantics web and React Native developers know. |
| iOS host | Swift + UIKit, the core linked as a static library | UIKit subclasses and delegates are trivial in Swift. Not SwiftUI: it is a second reconciler. |
| Android host | Kotlin + Android Views (Material Components 3), the core a shared library through JNI | Views are imperative targets for mount ops. Not Compose (a second reconciler, measuring only in its own pass). |
| Desktop hosts | In process. Windows: C on Win32, the core linked in (platform/windows). macOS and Linux, being replaced (RFC 0008): Go (AppKit through purego, GTK 4), driving the core through its C ABI (host/bridge) | They are hosts behind the ops wire like the others; their cost is the platforms' calls. No Go at run time once RFC 0008 is done; no cgo. |
| Web | The DOM, from a JavaScript host (platform/web), the core as WebAssembly | Batches decoded in place out of the WebAssembly memory; the browser's own elements. |
| C compiler | The platform's own (Xcode's clang, the NDK's, the system's); Zig's opted into (`ASUKA_CC=zig`) and to cross-compile | What the platforms' SDKs are tested with; Zig for what they can't do. |
| Packages | The npm registry, with the asuka CLI as the client (`asuka add`, `asuka.lock`) | A registry with scopes, auth and private mirrors for free; one version of each package per app. |
| Manifest | `asuka.toml` | Written by people, commented, merged by git. |

Open decisions, with the default until they are settled:

- Minimum versions: **iOS 16, Android API 33 (13), macOS 11**.
- Android widgets: Material Components 3, with Material 3 Expressive.

## Architecture

```
app code (Asuka)        components, stores, state
─────────────────────────────────────────────────────────────  lang/ (Go, in the CLI)
compiler                parse, check, lower: bytecode (dev), C (release)
─────────────────────────────────────────────────────────────  core/ (C)
vm/  rt/                the VM, values, tasks, the standard library
signal/                 signals, memos, effects, owners
ui/                     elements, the node tree, the flush
layout/                 block, flexbox, grid
ops/                    mount operations, binary encoding
abi/                    asuka.h: what hosts call
─────────────────────────────────────────────────────────────  asuka.h
platform/ios (Swift)   platform/android (Kotlin)   platform/web (JavaScript)
platform/windows, platform/linux (C, the core linked in)   platform/desktop (C they share)
host/darwin (Go, through host/bridge)
core/host/fake          tests: records ops, measures text at 8 points a character
```

The core never knows a host: it talks to them through `asuka.h` only.

### The C ABI

core/include/asuka.h is the whole contract. The host gives the core its
callbacks once, and calls in on the main thread:

```c
typedef struct {
	void (*apply)(const uint8_t *buf, size_t len);      // a batch of ops
	void (*measure)(uint32_t id, float width, float height,
	                float availWidth, float availHeight,
	                float *outWidth, float *outHeight); // a leaf's size
	void (*post)(void);                                 // call asuka_run_posted soon (any thread)
	/* native packages: native_new, native_call, native_start, ... */
} asuka_host_callbacks;

void asuka_start(asuka_host_callbacks *callbacks);
int  asuka_load(const uint8_t *module, size_t len);    // bytecode (dev); release builds link the app
void asuka_event(const uint8_t *buf, size_t len);      // an encoded event
int64_t asuka_list_count(uint32_t list);
void asuka_list_bind(uint32_t list, int64_t index, uint32_t cell);
void asuka_list_unbind(uint32_t list, uint32_t cell);
void asuka_run_posted(void);
```

## The language and its compiler

docs/language.md is the language; lang/LOWERING.md how each construct
becomes bytecode and calls into the core. The compiler (lang/syntax,
lang/types, lang/ir, lang/bytecode, lang/cgen, lang/compile) runs in
the CLI. In development `asuka dev` sends the player the app's
bytecode, and on each save the packages that changed: the VM swaps the
code, keeps every state cell by its component path and name (converted
to the new types by field names), and keeps the native views that are
still there. For release, `asuka build` compiles the app to C and links
it with the core.

## signal

core/src/signal ports [alien-signals](https://github.com/stackblitz/alien-signals)
(push dirty flags, pull values; glitch-free, no unnecessary
recomputation), with Solid's ownership model. The graph holds no
values: the VM's cells and memos own them and tell the graph when they
read or change one.

Rules:

- Every effect and memo belongs to the owner that was current when it
  was made. Disposing an owner disposes its children, runs their
  cleanups and unsubscribes them. Disposal is explicit and tested.
- Writes during an event handler are batched until the handler returns
  (or awaits). Writes are legal only on the main thread; worker threads
  only compute values (`spawn`).
- Render effects (the UI's bindings) run before user effects within a
  flush. A write inside a memo is an error; effects that keep dirtying
  each other stop after 100 passes, naming the effect.
- No allocation per write in the steady state: links are reused and
  kept on a free list.
- Errors: a computation that fails raises its error, which goes to the
  nearest catcher of its owner (a `Boundary`), or the app's.

## ui

core/src/ui is the node tree over native views and the flush. Compiled
code drives it through one protocol (core/src/ui/ui.h), the same for
every element: make an element, set or bind its keys (a key is a wire
prop of its native view, a style field, or a behavior such as a text
field's two-way text), add handlers, children and slots. The elements,
their keys and events are declared once (lang/prelude/elements.go),
which generates the C constants and the prelude the compiler reads.

### Components

A component is declared in Asuka, and compiles to a function that runs
**once** per instance and builds the bindings its body declares:

```asuka
component Counter {
  state count = 0

  Column {
    gap: 8; padding: 16
    Text("Count: {count}")
    Button("Add") { on press { count += 1 } }
    if count > 10 { Text("That's a lot") }
  }
}
```

Each prop whose value reads state is a render effect that sends only
that prop when it changes; a constant prop is sent once and makes no
effect. A style is a set of keys like any other.

### Control flow

- `if` (`ak_ui_show`): makes and disposes the branch's owner as the
  condition changes.
- `for … key` (`ak_ui_for`): keyed; a row's owner lives as long as its
  key. A moved row moves its native view; a changed item updates that
  row's bindings and nothing else. Directly in a `List`, rows are
  virtualized (`ak_ui_lazy_for`).
- `match` (`ak_ui_switch`), and views rebuilt from what they read
  (`ak_ui_dynamic`).

### Elements

| Component | iOS | Android |
|---|---|---|
| `View` / `Row` / `Column` | UIView | ViewGroup (no layout of its own) |
| `SafeArea` | insets from `safeAreaInsets` and the keyboard's frame | insets from `WindowInsetsCompat` (system bars, cutout, IME), edge to edge |
| `Text` | UILabel | TextView |
| `Button` | UIButton (`UIButton.Configuration`) | MaterialButton |
| `TextField` | UITextField | EditText (TextInputLayout optional) |
| `Toggle` | UISwitch | MaterialSwitch |
| `Slider` | UISlider | Slider (Material) |
| `Image` | UIImageView | ImageView |
| `Scroll` | UIScrollView | NestedScrollView / HorizontalScrollView |
| `List` | UICollectionView (list layout) | RecyclerView |
| `Refresh` (pull to refresh, on `Scroll`/`List`) | UIRefreshControl | SwipeRefreshLayout |
| `Stack` (navigation) | UINavigationController | own view stack with transitions, `OnBackPressedDispatcher` and predictive back |
| `Tabs` | UITabBarController | BottomNavigationView |
| `Tab`'s `badge` | UITabBarItem.badgeValue | BadgeDrawable (getOrCreateBadge) |
| `Destination`'s `badge` | UICellAccessory.label | a counter (MenuItem's action view) |
| `Alert` | UIAlertController (alert, action sheet; `addTextField` for its fields) | MaterialAlertDialogBuilder (its fields: TextInputLayouts) |
| `Snackbar` | the host's own: a capsule in the system material (Liquid Glass on iOS 26) | Snackbar (Material), in an overlay CoordinatorLayout |
| `Sheet` | sheet presentation (detents) | BottomSheetDialog |
| `Sheet` (`FullScreen`) | `.fullScreen` presentation | full-screen Dialog (Material's full-screen dialog) |
| `Menu` (context, button menus) | UIMenu / UIContextMenuInteraction | PopupMenu |
| `Activity` (spinner) | UIActivityIndicatorView | CircularProgressIndicator |
| `Picker` (a menu of options) | UIButton with a menu (`changesSelectionAsPrimaryAction`) | exposed dropdown menu (TextInputLayout, MaterialAutoCompleteTextView) |
| `ProgressRing` | drawn (SwiftUI's circular Gauge's look); UIActivityIndicatorView without a value | CircularProgressIndicator |

An element a host can't show yet is logged by that host with its name,
so gaps are found, never silently drawn wrong. The fake host
(core/host/fake) supports all of them.

Gestures on any `View`: `on press`, `on longPress`, `on pan`, `on
swipe`, `on pinch`, mapped to UIGestureRecognizers and Android's
`GestureDetector`. Haptics: `haptic(.success)` (UIFeedbackGenerator,
`performHapticFeedback`).

### Navigation

`Stack { Root() }` holds a stack of screens; `push(Detail(id))` and
`pop()` act on the nearest one (`ui.push` takes a builder: the screen is
built as it's pushed). A `screen` is a component whose block takes the
screen's props (`title`, `largeTitle`, `backGesture`, ...) and slots
(`actions`, `toolbar`, `primary`). A screen is an owner: popping it
disposes it. The host animates
pushes, pops and the interactive back gesture natively; a gesture the
user finishes tells the core with `Event(stack, popped)`, which disposes
the screen, and one they cancel changes nothing.

How the kinds map (ops/schema.go has the wire contract):

- A **Screen** (a stack's screen, a tab's page) and a **Sheet** are
  *environment providers*: the host sizes them and reports their own
  `Environment` (size, safe area, keyboard over them); the core lays each
  out as a separate root, and `SafeArea` inside reads the nearest one.
  A screen under a navigation bar has no top safe area; a page above a
  tab bar has no bottom one.
- A Screen's **Toolbar** and **Primary** (encoded as menus) are its
  bottom bar and main action, which the host shows as its platform does:
  UINavigationController's toolbar (the primary action its prominent
  trailing item), Material's BottomAppBar and FloatingActionButton (the
  screen's own views, so they go with it in transitions), the window's
  toolbar on a Mac, a command bar on Windows, a GtkActionBar in an
  AdwToolbarView on Linux, a bar and a floating button on the web. The
  bar is in the screen's bottom safe area (iOS, Android), or the screen
  is sized above it (the desktops, the web). Their
  Action events carry ops.ScreenToolbarActions or ScreenPrimaryAction
  plus the item's number, apart from the bar buttons'.
- An **Insert** into a Stack pushes (animated once it's on screen); a
  **Destroy** of its top screen pops, animated. The host keeps the dying
  screen's views until the animation ends: the Destroys of its
  descendants that follow are no-ops. A pop the user makes is the same,
  started by the host: it animates, sends `Popped`, and the core
  destroys the screen.
- A **Sheet** and an **Alert** aren't inserted anywhere: `Present` shows
  them, Destroy dismisses them, the user's dismissal sends `Dismissed`,
  an alert's button sends `Action` (and isn't a dismissal). A sheet's
  `FullScreen` comes before its `Present`, which shows it as it was: it
  covers the screen (a desktop window's content), and its Environment
  is the screen's own, safe area and keyboard included. An alert's
  `Fields` (ops/alert.go) are text fields: the user's typing sends
  `FieldChanged` (the field's index, its text), which writes the
  field's signal, and its buttons' `Disabled` follow; return in the last
  presses the default button (the first `Default` one) if it's enabled.
- A **Snackbar** isn't inserted either: `Present` shows it at the bottom of its
  window, Destroy takes it out, animated. Its action sends `Action`;
  its time running out or the user's swipe sends `Dismissed`; the core
  destroys it after either. The core keeps one per root: showing
  another destroys the one showing first. Its timer is the host's
  (the platform's recommended timeouts), never the core's.
- iOS: `UINavigationController` (its interactive pop gesture),
  `UITabBarController`, sheet presentation controllers with detents,
  `UIAlertController`, `UIMenu` (button primary action, context menu
  interaction; nested and inline menus, disabled and checked items),
  `UIRefreshControl`, feedback generators.
- Android: a `MaterialToolbar` over the screens, with Material motion
  (emphasized easing, slide and fade); back and **predictive back**
  through an `OnBackPressedCallback`, which follows the gesture (the top
  screen shrinks and follows the finger) and pops the innermost shown
  stack that can pop (the hidden tabs' stacks keep their screens); a
  `BottomNavigationView` (SF Symbol names map to Material icons); a
  `BottomSheetDialog` (medium is half expanded; the keyboard expands it,
  as iOS does); `MaterialAlertDialogBuilder` (an action sheet, or more
  than two buttons besides cancel, is a list); `PopupMenu`;
  `SwipeRefreshLayout` around the scroll view or list; haptic feedback
  constants.

#### Routes

A `Router` gives every screen an address, an href (a path and a query,
`/chat/7?tab=info`), as expo-router does. Routes are elements, declared
where the app's structure is (lang/LOWERING.md, "Routes"): a tree of
`Route`s, each a path pattern (static segments, `:param`, a last
`*catchAll`; relative to its parent's unless it starts with `/`) whose
block builds a screen from its `Params`, or holding routes under a
navigator (`layout: .stack`, `.tabs`, `.split`, `.slot`) or in a group
(no layout: a path prefix, a guard, a sidebar section). A `screen`
declared `at "pattern"` is a route of its own, its parameters typed and
read from the path or the query. A route with `sheet: true` is
presented over its navigator instead of pushed or chosen; `redirect:`
replaces an href with another; a `guard` (`fn(string) -> string`) sends
the user elsewhere (`""` lets them in). The runtime (core/src/ui's
router) builds the tree once, logging shapes the navigators can't show
(a tabs inside a stack: two bars), and ranks the screens' full patterns
as expo-router does (static before parameter before catch-all, segment
by segment); its top level is a slot of the routes given it and a
not-found catch-all. Nothing on the hot path: routing runs as the user
navigates.

The navigation state is a tree, react-navigation's shape: each
navigator's entries (a stack's screens bottom first; a tab's or a
destination's or a slot's chosen entry, each tab keeping its own) and
the sheets over it, each entry a route with its parameters and query,
or a navigator. Every change is made on a description of the state
(from the live one with each entry's key), then the live state is
reconciled to it, as a view diff: entries both have are kept (by key;
by route and parameters for a description from outside, a history
entry, a reload), the stack's others popped (the lower first, so one
pop animates) or pushed, the sheets dismissed or presented, the tab,
destination or slot switched. The views are the existing elements'
(`Stack`, `Tabs`, `SplitView`, `Sheet`, and a slot: a fragment whose
child is swapped), which report the pushes, pops and choices they
didn't make (the user's). An entry is an owner as any screen is: one a
route pops, replaces or leaves is disposed; a navigator closes its
sheets as its owner goes.

- **Navigating**: `navigate(href)` (or `navigate(Thread(id: 7))`)
  switches the tabs, destinations and slots on the way, goes back to an
  entry with the same address in the target's stack or pushes it,
  presents a sheet (react-navigation's `navigate`); `navigate(href,
  .push)` always pushes; `.replace` replaces the focused entry (a sheet:
  closed and the screen pushed); `router.back()` pops the innermost
  stack or sheet; `router.setQuery(q)` changes the focused screen's
  query, which its `Params` follow. A View's and a Button's `href`, and
  a rich text's link to one of the app's hrefs, navigate. `push(X(…))`
  stays: a screen pushed on a routed stack is an entry without a route,
  at its stack's address.
- **An href builds a state** (`fresh`): in each stack on the way, the
  stack's initial entry (expo-router's `initialRouteName`; its first
  route by default), then its screens whose patterns are prefixes of the
  target's (`/inbox/:id` under `/inbox/:id/reply`), then the target,
  each taking its parameters from the target's by name and left out if
  it can't (a parameter missing, a guard); the other tabs at their
  first entries. Redirects and guards are resolved first, at most 32
  steps (more is a loop: an error naming the hrefs).
- **Guards follow state**: a render effect asks the guards of the
  entries shown, and again as what they read or the state changes; one
  that keeps the user out drops those entries (with what's over them in
  their stacks; the other tabs are kept) and the router goes where the
  focused href resolves now, replacing it.
- **The state serializes** (entries by their route's key and their
  href, JSON), so it reads back after the routes change, as far as they
  still match. The router sends its address with every change
  (`Location`, to its window's element: the root or a `Window` node):
  the focused routed entry's href, the state, the window's key, and how
  the history moved, decided as react-navigation's linking decides:
  where the old and new focused chains part, a navigator's history
  length grew (Push), shrank (Back by as many) or stayed (Replace); a
  stack's length is its entries', a tabs' or a split's is 1 on its first
  entry and 2 on another ("firstRoute"), plus their sheets. Redirects
  and guards replace. A host that keeps windows' addresses gives the
  last back as the router mounts.
- **Where it starts**: a launch URL it takes (a link that came before
  any handler: `on openURL` gives it to the routers first, once the
  root is built), else the window's address (the web's address and
  history entry; a desktop window's last run), else
  `Router(initial:)`. URLs the app is opened with later (its scheme's,
  universal and app links, notifications') go to the first router that
  takes them, the main window's first: `scheme://a/b` is `/a/b` (the
  host the first segment, as expo-router reads it), an https link its
  path; the rest go to `on openURL`.
- **The web** keeps the address in the page's (history.js):
  `pushState`, `replaceState`, back to the latest entry with the href
  (none: replace, a deep link's screens popped one by one), each entry
  holding its index and the router's state, so back, forward and a
  reload rebuild the screens. The user's back and forward send the
  entry to the core (`Location` event); while it handles it, its answer
  only rewrites the entry, and a back of one that a screen's or sheet's
  `onBack` takes answers `Forward`, which goes back to the entry the
  user left. Routed stacks and sheets (`Routed`) go back through the
  history; the host's own back entry stands only for what the router
  doesn't route (an alert, a sheet of the app's). `<base href>` is the
  site's path (`web.base`), and the dev server and the static site
  answer every route's path with the page.
- **The desktops** keep each window's last address with where the
  window was (`internal/routestate`, `asuka-routes.json` in the app's
  data directory, written a moment after it changes, off the main
  thread): a window comes back at its screens. Phones don't keep it
  across launches, as their native apps don't.
- **Typed links**: the compiler knows each routed screen's full pattern
  from the one Router that routes it, so `navigate(Thread(id: 7))` and
  `href: Thread(id: 7)` are checked as the app compiles (a screen two
  Routers route, or none, is an error), and a constant string href is
  checked against the routes.

### Style

Every element takes the style props, which are keys like any other
(lang/prelude/elements.go): a constant is set once, a value that reads
state is a binding that sends only that prop as it changes. A `style
name { … }` declaration is a set of them, inlined where it's used; a
later prop of the same key wins.

Layout props map to CSS flexbox with React Native's defaults (column
direction, border-box): `width`, `height`, `minWidth`, `maxWidth`, ...
(points, `pct(50)`, `.auto`), `grow`, `shrink`, `basis`, `direction`,
`justify`, `align`, `alignSelf`, `wrap`, `gap`, `padding`, `margin`,
`absolute` with `top`/`right`/`bottom`/`left`, `aspectRatio`, and `grid`
(columns, rows, auto tracks, flow) with `gridColumn`/`gridRow`
placement. `borderWidth` insets the content, as the native border is
drawn inside the view.

Visual props, applied where the native widget supports them and ignored
elsewhere (documented per host): `background`, `radius`, `border`,
`shadow`, `opacity`, `hidden`, `font` (size, weight, family), `color`,
`textAlign`, `lines`. Defaults are the platform's own; an app that sets
nothing looks native.

Colors and fonts can be semantic (`.label`, `.secondaryLabel`,
`.accent`; `.body`, `.title`), resolved by the host (UIKit's semantic
colors and text styles; Material 3's color roles and type scale), so
dark mode and Dynamic Type / font scale need no app code. An enum's
cases are written with a leading dot where the type is known, so a
color can't be read as a weight.

A font's family (`Font(family: "Inter", style: .title)`) takes a font
file of the app's, by its name in `assets/fonts`; a family of them
(`Inter`: `Inter-Regular`, `Inter-Bold`..., picked by the weight); a
generic family (`serif`, `monospace`, `rounded`: on iOS the system
font's designs); or a system family. It scales as its text style does.
Under `asuka dev` the core sends a font file's URL instead, since the
player hasn't the app's assets: the host loads it, then sends
`FontLoaded`, and the core measures text again (its measure cache
cleared).

### Native packages

Platform code an app needs that Asuka doesn't wrap lives in **native
packages** (docs/native.md; `internal/nativegen`, `asuka native`): an
Asuka package declares a module, a `native interface`, or a view, a
`native component`. Each platform implements it in its own language, in
a project of that platform's own beside the Asuka:

- `Package.swift` (sources in `swift/`) for iOS and macOS;
- a Gradle library module in `android/`;
- `web/<pkg>.js`;
- C for Windows and Linux (`desktop/*.c`; `_windows.c` and `_linux.c`
  for one of them).

The generator writes the typed glue on every side:

- Swift: protocols, structs and enums, a provider class
  `@objc(AsukaModule_<path>)`.
- Kotlin: interfaces, data classes, `object AsukaModule`, in the
  package `KotlinPackage(path)`.
- JavaScript: frozen enums and a provider registered by import path.
- C: `desktop/<pkg>.asuka.h` (the types as structs and enums, a vtable
  per interface and per view, the `<pkg>_make_<T>` functions the package
  writes, async methods' `_done`, callbacks' `_call`, views' events)
  and `<pkg>.asuka.c` (the codec and a provider, registered by the
  package's path as the program starts, with the registry,
  platform/desktop/src/native.c).

The platform side writes only `makeT` functions. The compiler lowers a
`native interface`'s construction and methods to `native.new`,
`native.call` and `native.start` (lang/LOWERING.md), and a package's
`native fn`s to builtins the core implements. The types are the
scalars, strings, bytes, times and durations, arrays, maps of strings,
optionals, the package's structs and enums, native interfaces (as
parameters or results), and functions (notifications, run on the main
thread; questions, run synchronously).

Values cross as one buffer each way, in a fixed little-endian layout
(core/src/native; `asuka_writer`/`asuka_reader` in C), with no reflection
or JSON:

- the arguments in declaration order;
- the answer's status byte: ok, a declared error by index, a platform
  error (domain, code, message), unsupported, or canceled.

A call goes through the host's callbacks in `asuka_host_callbacks`
(`native_new`, `native_call`, `native_start`, `native_cancel`,
`native_release`):

- iOS: the Swift host's C function pointers;
- Android: JNI into `dev.asuka.AsukaModules`;
- macOS: the Swift library's `@_cdecl` functions, reached by host/bridge
  through purego;
- Windows and Linux: the registry (platform/desktop: the C hosts fill
  their callbacks with `asuka_native_callbacks`), which finds a call's
  provider by its class and keeps the objects by handle;
- the web: the JavaScript host.

A method marked `async` is a task: `native_start` begins it (Swift
`async`, Kotlin `suspend`, JavaScript with an `AbortSignal`), the
platform answers with `asuka_native_done`, and cancelling the task (its
owner going) calls `native_cancel`. Objects are handles; the core
releases one as its last reference goes, and the platform's last
reference goes on its main thread.

A `native component` is a `Native` view of the kind its package names
(the module's Swift class or Kotlin class or import path, `#`, the
view), each prop its encoded bytes (the wire's bytes value), each a
binding like any prop. Each host finds a `#` kind through the module's
provider, `makeView`, and, on Windows and Linux, through the registry
(`asuka_native_view`, `_prop`, `_measure`, `_destroy`; its events go
back through `asuka_native_emit`).

What a package's C may use of the C hosts is `asuka_desktop.h`
(platform/desktop/include): posting to the main thread (one FIFO with
the core's posts), a view's window, the app's name, ID, version and
directories, colors, HTTP, local files; `asuka_windows.h` and
`asuka_linux.h` hold what packages share on each (COM and WinRT; GLib,
GIO and GTK declared and resolved at run time).

`Native` is the mechanism under it: a node whose `Type` prop, sent right
after `Create`, picks the factory, with props of bools, ints, floats,
strings and bytes, and events back by name. The hosts' own native views
(`asuka.camera`, `asuka.map`) use it directly.

The CLI generates the app's own native packages' glue before each
build (others must ship current generated code, which is checked by
hash). It builds:

- iOS: one `xcodebuild` of a root package depending on the materialized
  host package `asuka` (the iOS host and the app's feature defines) and
  the app's native packages, its objects merged with `clang -r`, its
  frameworks embedded and its bundles copied;
- macOS: the same per architecture, into the Swift library, then lipo;
- Android: each module included as `:native-<pkg>` (`-PasukaNativeModules`),
  given the SDK levels and `:asuka`;
- the web: each `web/` shipped at `packages/<pkg>/`, imported before the
  app starts;
- Windows: a package's Windows code is C (`windows/*.c`), compiled with
  the host's headers into the app's one executable
  (`appbuild.WindowsOptions.Natives`);
- Linux: a package's Linux code is C (`desktop/*.c`, asuka_linux.h's
  GTK), compiled into the app's one executable with the host
  (`appbuild.DesktopNatives`).

A package's `asuka.toml` (usage strings, background modes, permissions),
`Info.plist`, `iOS.entitlements` and `macOS.entitlements` merge into the
app's: usage strings defer to the app's asuka.toml, arrays join, and
other keys are set if absent. Android's manifest merger takes the
module's `AndroidManifest.xml`.

### C from Asuka

`extern fn`, `extern type` and `extern struct` (RFC 0008, docs/c.md)
call C with no native package: the compiler generates C glue (lang/cffi)
whose wrappers are builtins, `ffi.<package>.<name>`, which an extern
fn's body calls with CALLB, so the VM and lang/cgen's C call them alike.
The CLI compiles the glue with the core (`-DAK_FFI`: ak_vm_builtins_init
calls its asuka_ffi_register) and the app's and its packages' `[c]`
sources with their own flags, into every target's build of the core:
iOS's static library (the executable links `[c]`'s libraries and
frameworks), Android's `libasuka.so`, the desktops' library, the web's
module, asuka-run, and the dev player, which asuka dev rebuilds when the
C or an extern declaration changes. The runtime side (extern types'
objects, callbacks posted to the main thread) is core/src/vm/ffi.c.

### Images, gestures, animation

- `Image` takes an http(s) URL or a path in the app's `assets/`
  (copied into the iOS bundle; pushed beside the library on Android).
  The host fetches, decodes (Android subsamples to the screen) and
  prepares it off the main thread, caches it in memory, and reports
  `Loaded` with its natural size: one point per pixel, as on the web.
  The core sizes the image itself: natural size, no wider than the space
  it has, the other side from the aspect ratio; a style's `AspectRatio`
  overrides the image's, so a box keeps its size before and after
  loading. A BlurHash or ThumbHash placeholder is decoded by the core
  (core/src/ui/imagehash.c, cached) and sent as pixels (`PlaceholderImage`);
  the host shows it, in its own mode, until the image arrives and fades
  in over it (`Transition`). An animated image plays: ImageIO's player on
  Apple platforms, `AnimatedImageDrawable` on Android, the browser's (or
  WebCodecs') on the web, and the host frame by frame on Windows and Linux
  (`internal/animimage`). The host reports `Animated` after `Loaded`, and
  only then does the core follow Reduce Motion for it (`Paused`), so a
  still image costs no effect.
- Gestures are `View` events (`on pan`, `on pinch`, `on swipe`) with the
  platform's recognizers on iOS (pan and pinch recognize together) and a
  touch listener on Android that claims the touch once it moves past the
  touch slop: it cancels the view's press and stops the parent
  intercepting. Android's `ScaleGestureDetector` isn't used: it ignores
  pinches narrower than about 27mm.
- `animate { … }` (or `animate(Animation(duration:, curve:)) { … }`)
  brackets the next flush's two batches with `AnimateBegin`/`AnimateEnd`; the host
  changes frames, opacity, background and corner radius inside UIView
  animations, and with animators on Android (a frame animates by
  interpolating the layout bounds; views that weren't on screen don't
  animate in).

### Interaction states, drag and drop

A view's interaction states (pressed, hovered, dragged, a drag over a
drop target) are reported by the host as events, and the core turns
them into what styles read (core/src/ui/interaction.c). There are three
layers, each built on the one below:

- **Variants**, for the view's own style: `hovered { … }`, `pressed {
  … }`, `dragging { … }` and `dragOver { … }` in a container's block.
  They are CSS's `:active` and `:hover`, and Tailwind's `active:` and
  `hover:`. While the view is in a state, its variant is laid over the
  style:
  - A variant sets only the props it names (a `font`'s fields one by
    one), and its values are bindings like any prop's.
  - Variants apply in the order hovered, pressed, dragging, dragOver.
  - A container with variants always makes a view of its own (it isn't
    flattened).
  - Other elements take no variants (the checker says so).
- **States as values**: `View as card { … }` (or `ref: card`) names the
  view, and `card.hovered`, `card.pressed`, `card.dragging` and
  `card.dragOver` are tracked bools (`ui.state`) any binding reads: a
  child's style (a group's hover), a sibling's (a peer's), a `Text`, an
  `if`. Compose has `InteractionSource` and Flutter
  `WidgetStatesController` for the same thing.
- **Handlers**, for side effects: `on pressedChange(p)`, `on
  hoverChange(h)`, a drop target's `on dropOver` and a drag source's `on
  dragPhase`.

A view listens for a state only from when something first asks for it:
a variant, a read of its state, a handler.

- A host may get the `Listen` long after `Create`, and every host
  handles that.
- A view that nothing asks for its pressed state doesn't report it, and
  keeps the platform's highlight.
- This is exact, not an approximation: a read that never happened can't
  have changed any output.

A view's `drag` items make it a drag source and its `dropAccept` a drop
target. The core reports each state change once, and keeps the states
last reported. A view disposed while in a state reports leaving it, to
its handlers and its readers, so what follows it elsewhere doesn't stay
stuck.

The wire (ops/schema.go): events `Pressed` and `Hover` (bool),
`DragOver` (a gesture payload: Began, Changed, Cancelled, and X, Y),
`Drop` (bytes: x, y, the source view, flags, items) and `Dragging` (a
`DragPhase`); `View` props `DropAccept` (MIME patterns) and `DragItems`
(the items, each with its kind, text, path, name, MIME type and whether
it carries an app's value). `ops/item.go` has the codecs and
`ops.MatchMIME`, which the desktop hosts share (the core's are in C). What every host does:

- **Pressed** (a `View` listening for `Pressed`). True as the primary
  pointer goes down on the view (a touch, the mouse's primary button);
  only the innermost: a touch on a descendant that is pressable itself
  (`Press`, `LongPress`, `Pressed`, a tap menu) or a control (a button,
  a switch, a field) doesn't press its ancestors, as UIKit's cells and
  Android's clickable views behave. In a scrolling ancestor it comes
  after the platform's tap delay (UIScrollView's 150 ms, Android's tap
  timeout), so a scroll doesn't flash it; a tap released sooner reports
  true, then false a moment later (Android's pressed-state duration,
  about 64 ms; iOS likewise), so the look shows. False as it lifts;
  as it moves off the view (back on, while still down, true again;
  iOS keeps a 20-point margin, as UIControl's tracking does, Android
  its touch slop); as a
  scroll, another gesture (a pan, a swipe), a long press or a drag
  takes the touch (the press then doesn't fire); as the app goes to the
  background or the view leaves the window. `Press` fires only for a
  release while pressed, so the two agree. Only the first touch counts.
  A view with `Pressed` and no `Press` (a hold to record) tracks presses
  without becoming a button for screen readers. Windows: Space held on
  a focused pressable view presses it, its release fires `Press`.
- **Hover** (a `View` listening for `Hover`). True as a pointer (a
  mouse, a trackpad, an iPad's pointer, a hovering pencil or stylus)
  comes inside the view's bounds; it stays true over its descendants,
  as CSS's `:hover` does, and goes false as the pointer leaves the
  bounds, the window or the app, or the view is hidden or removed.
  iOS: `UIHoverGestureRecognizer`, recognizing with the others. Android:
  the container's `dispatchHoverEvent`, which sees the pointer over its
  children too (its `onHoverEvent` gets an exit as a hoverable child
  takes the pointer). macOS: an `NSTrackingArea` (entered and exited,
  in the visible rect, active in the active app), checked against the
  mouse as it's added, so a view that appears under the pointer is
  hovered. Windows: tracked from the message loop: each mouse move finds
  the views under the cursor, the window it's over and its ancestors,
  and `TrackMouseEvent`'s leave clears them as it leaves the app's
  windows.
- A view that reports its pressed or hover state draws its own: the
  host leaves off its own highlight (Android's ripple foreground,
  Windows' hover and pressed fills; the focus ring stays).
- **Drop targets** (`DropAccept`). A drag is taken if any of its items'
  MIME types matches a pattern (`ops.MatchMIME`: exact, `type/*`, `*/*`
  or `*`, without regard to case or parameters). An item's type: a text
  `text/plain`, a link `text/uri-list`, a file's from its UTI or
  extension (unknown: `application/octet-stream`), image data its
  image type, an item dragged from the app its `DragItems` MIME type
  (the core fills it in). Only the innermost target under the pointer
  that takes a drag gets it. `DragOver` Began as a taken drag comes
  over the view, Changed as it moves (its place in the view, in
  points), Cancelled as it leaves, ends or is dropped. The platform
  shows a move for the app's own drags, a copy for other apps'. A drop
  from the app sends `Drop` at once, with its source view and no items
  (the core has them, Values and all, as they were when the drag
  began). Another app's drop loads the items the target takes, and
  only those, then sends `Drop` with them: texts, links (as
  `ShareKindLink`), files; on iOS and Android the files (and image data)
  are copied into the app's caches, as a share's are (flag 1: the app
  owns them), on macOS and Windows they're the files themselves; a
  macOS file promise is received into the caches.
- **Drag sources** (`DragItems`). Touch: a long press lifts the view
  (`UIDragInteraction`, enabled on iPhone too; Android's long click
  starting `startDragAndDrop`), and the long press then fires no
  `LongPress`. Mouse: a move with the primary button down past the
  system's drag distance, which cancels the press (`Pressed` false, no
  `Press`) and comes before a pan. The drag shows the view's image.
  Other apps get each item's text (plain text), link (a URL) or file
  (iOS: the file's provider; Android: a `FileProvider` URI with global
  read; macOS: a file URL; Windows: `CF_HDROP`); an item that's only a
  value goes nowhere else, and a drag of only values is kept to the
  app. The session carries its source view's ID in a private type (iOS's
  local context, Android's local state, a private pasteboard type or
  clipboard format with the process's ID on desktop), which a drop in
  the same process sends as its source. `Dragging` Began as it lifts,
  then exactly one of Dropped (an operation other than none) or
  Cancelled. Windows lets a drag that carries files copy or link only
  (never move them, Explorer included), so the app's own targets show
  a copy for those; its drops take virtual files too (Outlook's
  attachments, a browser's images), copied into the app's folder.
- With presses the view's own, iOS's `Press` follows UIControl (a
  release within the margin, not a tap recognizer's 10 points), and a
  macOS view's tap menu opens on a click's release, as on Windows.

### Keys

A hardware keyboard's keys, on a desktop, an iPad, a Chromebook, a
phone with one, and in a browser: what a view does with them while the
keyboard's focus is in it (its `keys` prop, a `[Shortcut]`, each with
its key, modifiers and what it runs; core/src/ui/keys.c), and what the
window does (`useKeys([…])` in a component's body).

- Declared, not asked: a view's keys are a prop (`Keys`: each key's
  name, a lower case character or a named key, and its modifiers, as the
  menu bar's), and the host routes a key going down (and each repeat) on
  its own: from the focused view out through its ancestors, the nearest
  with the key takes it, and the host sends `Key` with its index. So no
  host waits on the app to know whether a key is taken (the platforms
  want to know as the key goes down: a text field's caret, the menus'
  key equivalents).
- The nearest first, as the responder chains go (AppKit's, UIKit's,
  the DOM's bubbling): a field's own keys, then the views around it,
  then the window's (`useKeys`: the root's, or a Window node's, keys
  prop).
  A key goes out from the focused node through its parents in the
  core's tree. A node the core leaves without a parent, but which shows
  inside another, goes on where it shows: a List's row (a cell the host
  made and bound) to its List; a Sheet's node to the window it's
  declared in (its `Window` prop: the root's or a Window node's ID, the
  window it's presented on, as `useKeys` in it are that window's), a
  Popover's to the root of what its anchor is in (a window's root, or a
  sheet's node, for a popover in a sheet), so its way ends at that
  window's root and its `useKeys`; on a phone (one window), the root. A Window node is an end: its window's own keys (its
  `useKeys`), never the main window's; the root is one too. With
  nothing focused, a key starts at the root of the window that has the
  keyboard: the root, a Window node, or a sheet's or a popover's node
  (then out, as above). A key a view takes doesn't reach the focused
  control (Up and Down in a search field move a selection, not its
  caret) nor the menu bar.
- Matching: the key as it types without modifiers (a character, lower
  case: Shift is a modifier like the others), and exactly the declared
  modifiers. Never while an input method composes text (marked text):
  its keys are its own.
- `useKeys`' lists are merged per window, the last one called first for a
  key; each goes with its component's owner, and a view's with the view.
- Hosts: macOS, a local event monitor for key down: the first
  responder's view (a field's field editor: its field), its Asuka node
  (the window's root node with none: the root, a Window's, a sheet's or
  a popover's), out (a cell to its list, a sheet's node to the window
  it's declared in, a popover's to its anchor's window), ahead of the
  field editor and `performKeyEquivalent:`. Windows, the message loop's
  WM_KEYDOWN and WM_SYSKEYDOWN before TranslateMessage, IsDialogMessage
  and the accelerators: the focused HWND's node, out (a cell to its
  list, a sheet's node to the window it's declared in, a popover's to
  the window it covers, its owner). Linux, a key controller on each window, and each popover, in
  the capture phase: the focus widget's node, out (a list's cells are
  the host's views in it; a sheet's node to the window it's declared
  in, a popover's to its anchor's window). The web, a keydown listener
  on the window in the capture phase, ahead of the menu bar's:
  `event.target`'s node (the page's body: the root; a dialog's own
  element: its node), out (a cell to its list, a sheet's node to the
  window it's declared in, a popover's to its anchor's window), `preventDefault` and `stopImmediatePropagation`. iOS,
  `UIKeyCommand`s from the root view controller, which UIKit asks
  through the responder chain as a key goes down: the first responder's
  node's keys, out (a cell's to its List, a sheet's or a popover's node
  to the root); with no first responder, from the presented sheet's or
  popover's node, else the root. Each is
  `wantsPriorityOverSystemBehavior`, so they come ahead of a text
  field's caret, and they repeat while held. Android, the activity's
  `dispatchKeyEvent` (and a sheet's or popover's window, at its screen;
  with nothing focused there, its unhandled key, from its node): the
  focused view's node, out (a cell to its List, a sheet's or popover's
  node to the root); Ctrl is Primary, as Control.

### Accessibility

Native widgets bring their own. Every element takes `label`, `hint`,
`role` (button, header, image, …) and `a11yHidden` (hidden from screen
readers with what's in it), which hosts map to UIKit's accessibility
properties and Android's `AccessibilityNodeInfo` (through
`ViewCompat`). Layout-only views (below) never appear in the
accessibility tree.

- A pressable `View` is one element, a button, read as its contents'
  labels joined (VoiceOver; TalkBack merges a clickable view's text
  itself).
- `TextField` and `Toggle` (switch or checkbox) take a `label` that
  follows the app's language, carried by the common `A11yLabel` prop,
  without replacing the control's value or native editing and toggling.
- `announce(text)` has the screen reader say what changed where focus
  isn't (a toast, a count of results); `a11yFocus(ref)` moves it to a
  view; a view's `actions` are what a screen reader offers to do with
  it.
- A List row's swipe actions are its accessibility actions on Android
  (UIKit's list offers its contextual actions to VoiceOver itself).
- `environment().reduceMotion` is iOS's Reduce Motion, or Android's
  animations turned off; on iOS, `Transition`s then dissolve instead of
  moving, as UIKit's do (Android's animator scale already makes every
  animation instant).
- Text follows Dynamic Type and Android's font scale, custom fonts too.

## Flush

Writes mark effects dirty; the scheduler flushes once:

- at the end of each event dispatched from the host (synchronously, so
  the host shows the result in the same run loop turn);
- after work posted to the main thread (`ak_post`: a task's
  continuation, a timer, a native call's answer);
- never on a timer.

A flush:

1. Runs dirty effects. Prop bindings append `SetProp` ops; `if`, `for`
   and `match` append create, insert, remove and destroy ops.
2. `Apply` the batch: native views now exist with their props.
3. Lays out dirty subtrees (`layout`), calling `Measure` for leaves whose
   props or constraints changed (cached per node and constraints).
4. Appends `SetFrame` for frames that changed and `Apply`s that batch.

That is two crossings per flush, whatever changed.

A host may call back into the core while it applies a batch: Android
lays a list out at once when its frame is set, which binds cells, and a
bind flushes. Ops written meanwhile go into a spare batch, so the nested
flush applies only its own; the batch being applied is never sent twice.
Events a host raises while it applies (a screen reporting its new size
during layout) wait in the host until `Apply` returns.

**View flattening** (as React Native's Fabric): a `View`, `Row` or
`Column` with no visual props, no events and no accessibility props makes
no native view. Its children are placed in the nearest real ancestor,
offset by its frame. Events and accessibility props are fixed at
creation; a binding that gains visual props makes its container real
and the flush moves its children into the new view (it then stays real).
`if`, `for` and `match` are fragments: no box and no view, their
children splice into the parent's layout and native children.

## layout

Our own engine (core/src/layout) implementing CSS **block**, **flexbox**
and **grid** layout (in the spirit of Taffy, not a port of it or of Yoga), in points
(iOS points, Android dp; hosts convert to pixels).

- **Model.** A node is computed with known sizes (NaN when free), a
  percentage basis and the available space per axis: definite,
  max-content or min-content. A run either only sizes the node or also
  places its subtree. Leaves (text, controls) are measured by the host
  with the same inputs: `sizeThatFits:` on iOS, `View.measure` with
  `MeasureSpec`s on Android; min-content is the widest word.
- **Algorithms.** Flexbox per CSS Flexible Box §9 as browsers implement
  it (automatic minimum sizes, the freeze loop, intrinsic main sizes,
  wrap and wrap-reverse, baselines, auto margins, absolute children at
  their static positions). Grid per CSS Grid §8 and §12 (explicit and
  implicit tracks, `repeat` with `auto-fill`/`auto-fit`, `minmax`,
  `fit-content`, `fr`, sparse and dense auto-placement, spanning items,
  track alignment; lines clamped to ±10000 as browsers do). Block per
  CSS 2 (auto widths fill, vertical margins collapse between siblings,
  through empty boxes and with the parent's edges), each in either
  `direction`. Not yet: writing modes, floats, named grid lines and areas, sizing keywords
  (`min-content` etc. as widths), `safe` alignment.
- **Incremental.** Each node caches its final layout and eight
  measurements keyed by inputs; `MarkDirty` clears a node's caches and
  its ancestors'. A clean node asked the same thing answers at once, so
  changing one Text measures that Text and re-places only the boxes on
  the way up; nodes whose size is forced by tight inputs are never
  measured. A layout reports the nodes whose frame changed, so the flush
  sends only those `SetFrame`s.
- **Shared measure cache.** The UI runtime keeps one per root
  (core/src/ui/measure.c) (two generations
  of up to 1024 entries), keyed by content: the kind, the text or
  title, the font, the line limit, the variant, the font scale and the
  constraints. Nodes showing the same thing (recycled list rows,
  repeated labels) ask the host once.
- **Performance.** All scratch memory (flex items and lines, grid
  tracks and items, block items) lives in a reusable `ak_lpass`:
  steady-state layout allocates nothing (counted by the tests). Frames
  are snapped to the pixel grid by the UI runtime when it sends them: edges round to the nearest
  pixel, except that a measured leaf's size rounds up (rounding both
  edges can take a pixel off a label, and a label a pixel narrower
  wraps), as Yoga does for text.
- **Correctness** is defined by Chrome: `layout/testdata/css` holds
  Taffy's Chrome-generated fixtures (block, flex, grid; border-box,
  either direction), converted for the C engine
  (`core/test/testdata/layout_fixtures.txt`, `go run
  layout/testdata/gen_c_fixtures.go`) and run by `go test ./core -run
  Layout`, with what they don't cover in
  `rtl_chrome.xml` and `ltr_chrome.xml`, each rendered by Chrome; `known_failures.txt` lists the ones
  that don't match yet (mostly the unsupported features above).

## ops

The only contract between core and hosts. Its schema is written once in
Go (`ops/schema.go`); `go generate ./ops` writes the Go constants, the
Swift decoder (`platform/ios`), the Kotlin decoder (`platform/android`)
and the JavaScript decoder (`platform/web/src/ops.js`), each with event
encoders.
Mobile hosts read the binary encoding; `host/fake` (and desktop hosts
later) read the batch's Go structs directly.

```
NodeID   uint32, made by the core, never reused in a process
Kind     uint16 (View, Text, Button, … then registered Native kinds)
PropID   uint16 per kind, from the schema

Create(id, kind)            Destroy(id)
Insert(parent, child, index)  Remove(parent, child)
SetProp(id, prop, value)    value: bool | int64 | float64 | string |
                            color | edges | enum | bytes
SetFrame(id, x, y, w, h)    relative to the native parent, in points
SetRoot(id)                 the view that fills the window
Listen(id, event)  Unlisten(id, event)
Command(id, cmd, args)      focus, blur, scrollTo, list updates,
                            push, pop, present, dismiss, …
```

Binary encoding: little-endian, a length-prefixed buffer of ops, strings
as UTF-8 with their length. The Go side reuses one buffer per batch (no
allocation per op).

Events from hosts:

```
Event(id, kind, payload)    press, longPress, textChanged(string),
                            valueChanged, toggled(bool), scrolled(x, y),
                            refresh, focus, blur, submit, popped,
                            dismissed, back (Android, unhandled),
                            windowState, closeRequest (desktop)
Event(root, environment)    root size, safe area insets, keyboard overlap,
                            scale, appearance, text size, locale
Event(root, lifecycle)      foreground, background, memory warning
```

Synchronous requests from hosts (they return a value, so they are
function calls, not events): `ListCount(id)`, `ListBind(id, index,
cellID)`, `ListUnbind(id, cellID)`.

### Lists

A `for … key` directly in a `List` is virtualized (`ak_ui_lazy_for`,
core/src/ui/list.c), backed by UICollectionView and RecyclerView. Because the core is on the main
thread, the data source calls straight in:

- `ListBind` with a cell the host made: the core makes the row's owner
  under the cell (or reuses the one the cell last held, setting its
  `item` and `index` signals, which updates only changed props),
  flushes, and returns. Recycling a cell costs the props that differ.
- Row heights come from the core's layout of the row at the list's
  width (self-sizing cells measure through `ListBind`'s flush).
- `List { appearance: .insetGrouped }` is each platform's grouped
  list: UIKit's inset grouped list; Material 3 Expressive's segmented
  list on Android (each row its own container, 2dp apart, the group's
  ends rounded, on the surface container); a System Settings group on
  macOS; Windows 11's settings cards. The core owns the inset, from the
  platform's margins (iOS's layout margins, 16 or 20; Material's window
  margins, 16 or 24; 20 on macOS, 16 on Windows) and the width class,
  lays the rows out at the list's width less it on each side, and sends
  it (`Inset`); the hosts inset the rows by exactly that, not by their
  own margins, so what's measured is what's shown.
- Changes to `items` send `Command(list, listUpdate, item IDs)`: a
  stable int64 per key, in order. iOS applies them as an
  `NSDiffableDataSourceSnapshot` (once the call into the core has returned, so
  binding never re-enters a flush); Android diffs them with `DiffUtil`
  (likewise after the call returns) and dispatches item ranges and moves
  to the RecyclerView adapter. Bound rows whose item changed are updated
  in place by the core.
- Cells are views the host makes; their IDs have the high bit set. A
  cell's `SetFrame` carries the row's height.
- Sections (a `Section` in the `List`) follow the item IDs in the same
  `ListUpdate`: each section's first item and an ID of its own (stable
  while its key lasts, as an item's). A section's header is a cell too,
  bound with `ListBind`'s index `-1 - section`, so no new entry point
  crosses the C ABI or the dev socket. A plain list's header sticks at
  the top: a pinned supplementary view on iOS (iOS 26's scroll edge
  effect goes behind the pinned one only:
  `UIScrollEdgeElementContainerInteraction`, whose region takes
  touches); on Android, which has no sticky headers, a cell of its own
  over the RecyclerView, in a frame that is the list's view.
- The anchor (`anchor: .bottom`, a chat's) and keeping what's on screen in
  place as rows are added above are the host's, which knows where rows
  are: iOS remembers the first visible row and its offset before a
  snapshot applies and puts it back after, and pins the end in
  `layoutSubviews` while the user hasn't scrolled off it; Android's
  `LinearLayoutManager` keeps its anchor row itself (`stackFromEnd` for
  the bottom); the desktop hosts, which tile their own rows, keep the
  row at the top as rows bind and change height. `on startReached` is the
  core's, from the rows it's asked to bind.
- Selecting rows: the core sends `Selecting` and each cell's `Selected`;
  the host shows its control, reports the user's choices
  (`RowSelected`), and the room the control takes from the rows
  (`RowInset`), at which the core lays them out again.

### Text input

`TextField { bind text: query }`, where `query` is state. The widget
owns its text. Its edits write the state; the core remembers the last
value the native side reported and sends `SetProp` only when the state
differs from it, so typing never
round-trips and IME composition is never disturbed. Props for the
keyboard: type (text, number, email, URL, phone), return key, secure
entry, autocapitalization, autocorrection, content type (for AutoFill).

The selection (`bind selection:`) is kept the same way: the host
reports its moves (`SelectionChanged`, after the text change that
caused them) and the core sends one only when the state differs from
what was reported. The wire counts UTF-16 units, as UIKit and Android
do; the core converts to byte offsets of the UTF-8 string. A ref's
`insert(text)`
replaces the selection through the platform's own text input (UITextInput's
`insertText`, `Editable.replace`, the field editor's `insertText:`,
`EM_REPLACESEL`), so it's undoable and reported as typing. Content of
`pasteTypes` that's pasted, put in by a keyboard or dropped goes to the
app as files (`Pasted`, its items as `Shared`'s) instead of into the
text.

A rich field's styled text (`bind rich: spans`) goes to the host in
RichText's Content encoding and comes back in the same, canonically
(styles in the order runs first use them, runs of one style merged), so
the core tells the user's edit from what it sent, as with Value. Each
host keeps a run's style as it was sent beside the platform's
attributes (an attribute of its own, a span; Windows's rich edit has
neither, and goes through RTF, `internal/rtf`), so what the user didn't
change comes back as it was: bold, italic, underline, strikethrough,
monospace and links are read from what the text has. Formatting is
undoable with the typing around it: UIKit's, AppKit's and the rich
edit's undo cover it; Android's covers text only (and puts deleted text
back unstyled), so an Android rich field keeps its own history, the
text with its spans and the selection before each change, typing
grouped as editors do.

A measured view's padding and border are inside its frame
(`ContentInset`, sent when it isn't zero): the host draws its content
inside them (a label's text rect, a field's text rects, a text view's
container inset, view padding on Android) and measures its content
alone. A button's replace its own padding, as CSS's do (UIButton's
configuration insets, Material's insets, minimum size and padding, the
web button's padding), so it's sent with the style, before the host
measures, and again with the frame when a percentage resolves
differently. A button whose style sets no padding sends none, a border
alone included: it keeps its own padding, and its title is centered.
The desktop hosts keep their bezels' own padding inside the inset.

### Lifecycle and state

`on lifecycle(state)` in the app's block reports foreground and
background. On Android the core and the views live in the process, not the
Activity: views are made with the app's Material theme
(`ContextThemeWrapper` over the application). The Activity handles
rotation, size, dark mode, font scale and density itself
(`configChanges`); any other configuration change remakes it, and the
new Activity takes over the existing tree: `asuka_attach` resends the
root and every frame, with no app state lost. Android
views resolve theme colors and text sizes when they're made, so after a
dark mode, font scale or density change the host makes its text and
Material views again, replaying the props it was sent and keeping the
user's state (text, selection, checked, value). There's one Activity
(`singleTask`), and whichever instance comes to the front takes the
views. Process death loses state, as in any native app; what must outlive
it is stored (`asuka/storage`'s `Value<T>(key, default)` over
`UserDefaults` / `SharedPreferences`), not to be confused with keeping
state across `asuka dev` reloads (below), which needs no code.

The core can also start without a screen: when the system runs a
background task (`asuka/sdk/periodic`: BGTaskScheduler, WorkManager) with the
app closed, the host starts the core headless (`startHeadless`): `main`
runs, the root component mounts into views no window holds yet, and the
task's Go code runs. A screen that comes later (the user opens the app)
takes that root as a recreated Activity does (`asuka_attach` on Android;
the root controller mounts the existing root view on iOS).
`asuka/location`'s background updates and regions start it the same
way on Android (its `PendingIntent`s reach the package's
`LocationReceiver`, which starts the core headless); the package keeps
each event until the app reads it. On iOS, the system relaunching the
app for them (a region, a significant change) doesn't start the core
yet: a native package has no hook at launch, where the old host module
registered its delegate. Where
the system has neither (macOS's and Windows's updates, Linux's and the
web's regions), the Go package computes them from the positions while the
app runs (`locationspec.Capabilities` says which). The last root's
`Dispose` fails the Calls still waiting, so a package's goroutine ends
with it.

Packages that need a framework or library of their own cost only the
apps that import them: a std native package's code is its own (its
Swift package, its Gradle module with its libraries, its web module),
and what it needs of the app (permissions, purpose strings,
entitlements, services) is in its asuka.toml and beside its code, merged
into the apps that import it (std/README.md). The hosts keep only what
the core's elements need and the modules std packages call by name
(`asuka/host`, `asuka/platform`), and what of those links a framework or
library is built only into the apps that use it: on iOS, under a flag
(`ASUKA_VIDEO`, `ASUKA_NOTIFY`...) for an element the app shows
(`elementFlags`) or a package's manifest's `ios.hostFlags`; on Android,
an optional Gradle module (`asuka-video` for a Video's Media3,
`asuka-push`'s Firebase, `asuka-periodic`'s WorkManager), for an app
that shows a Video or whose packages ask for it (`android.hostModules`);
asuka dev's player has them all. What an app uses is what the checker
resolved across its program, its dependencies' code too
(appbuild.Uses: the packages imported, and the members referred to from
outside their own package), not the identifiers its sources spell: a
package's needs apply to the apps that import it, a `[uses.<member>]`
table's to those that refer to the member (asuka/location's background
part, iCloud's store or documents). The host makes their views and hooks by name
when first used (`AsukaVideo`, `AsukaPlugin`).

In-app purchases (`asuka/purchase`) are a std native package: StoreKit 2
in its Swift (iOS; the Mac's Go host still builds platform/macos's
`Purchase.swift` into the app's Swift library: its manifest's
`macos.hostFlags`), Play
Billing in its Gradle module, the Microsoft Store (WinRT's StoreContext) in its desktop C, the
Digital Goods API in its web module; none on Linux; `purchase.fake()` in
tests. Each follows the store's transactions from the package's start
(StoreKit's `Transaction.updates` and its unfinished ones, Play's
listener and its unacknowledged purchases at each return to the front,
the Store's license changes) and keeps them for the package's `events()`,
so a purchase completed before the app observes isn't lost.

iCloud (`asuka/sdk/icloud`) is one module, `icloud`, whose JSON
(`internal/icloudspec`) the iOS host and the Mac speak, the same Swift
in both (`ICloud.swift`, kept identical in platform/ios and
platform/macos, as `Purchase.swift` is; `ASUKA_ICLOUD`, asuka/icloud's
manifest's `hostFlags`): NSUbiquitousKeyValueStore, its values converted
between JSON and property lists; the app's ubiquity containers through
NSMetadataQuery, NSFileCoordinator and NSFileVersion, the file system's
work off the main thread. The other hosts answer not available
(`icloudspec.Unavailable`); `fake.ICloud` in tests. The package's events
(the account, the store's changes, each scope's files) come through one
`next`, asked again from its answer on the main thread, so a root's
disposal ends it. What the app calls, from its source (`useICloud`), says
which entitlements it gets, which the CLI signs as Xcode does (a
profile's `*` and `TEAMID.*` as the app's values).

What runs outside the app's process is built from `asuka.toml`. iOS's
widget extension is WidgetKit's, so SwiftUI's (the one place the host
uses it): one template (platform/ios/Widget) with the app's kinds,
reading the entries the app's Go code sets (`asuka/sdk/widget`) from the App
Group they share; it runs no Go. The same extension draws the Live
Activities (`asuka/sdk/liveactivity`), for an app whose Go code calls the
package (`ASUKA_LIVEACTIVITY`), with or without widgets: one template,
its attributes (`AsukaActivityAttributes`) compiled into both the host
and the extension, since ActivityKit matches them by name and shape. On
Android an activity is an ongoing notification the host builds, which
Android 16 promotes to a Live Update. The shortcuts are App Intents the CLI
generates and links into the executable, their metadata extracted as
Xcode does (swiftc's constant values, appintentsmetadataprocessor); each
opens the app with its URL. On Android, the CLI generates an App Widget
provider for each kind (over the host's `AsukaWidgetProvider`, with
RemoteViews) and the launcher's static shortcuts.

Two extensions run the app's Go code, from the app's own symbols rather
than `asuka.toml`: the share sheet (`app.ShareSheet`) and the
Notification Service Extension (`push.Rewrite`). With either, the app's
Go code and the host are one dynamic framework (AsukaApp.framework) that
the app and the extension link, their data in the App Group they share.
The extension starts the core headless with a root of its own
(`ASUKA_SHARE_SHEET`, `ASUKA_NOTIFICATION_SERVICE`). Android runs both in
the app's package: the share sheet's activity, and the push rewrite in
Firebase's service, starting the core headless as a background task does.
Notifications' responses work the same way: a background action starts
the core headless (iOS's center delegate, Android's broadcast receiver)
and hands Go the response, keeping the process until Go reports it done.

### Plugins

A plugin is a Go package with a manifest beside it, `asuka-plugin.toml`
(package `plugin`), naming the native libraries it brings: for each
target (`darwin`, universal; `ios`, `ios-simulator`; `android-<goarch>`;
`linux-<goarch>`; `windows-<goarch>`; `wasm`) a file, published to the
plugin's GitHub release gzipped and named for its hash, with the
SHA-256 of both. Only the manifest is checked in. The plugin's Go code
loads its library through purego or `syscall`, from the path
`plugin.LibraryPath` gives it: where the CLI shipped it with the app;
else, in `go run`, `go test` and `asuka dev` on a desktop, the plugin's
checkout's build (`build/<target>` beside the manifest) or the
published file, downloaded once into the user's cache and verified. A
shipped app (`asuka_bundled`) and a phone's never download.

The CLI finds an app's plugins among its dependencies for the build's
platform (`go list -deps`, a manifest beside the package) and ships each
library's file where `LibraryPath` looks: `Contents/Frameworks` on macOS
(signed with the app), beside the `.exe` on Windows, `lib/<exe>` of the
prefix on Linux, a framework of its own on iOS (named for the library;
an Info.plist, a privacy manifest from the binary's imports, signed with
the app), beside `libasuka.so` in each Android ABI, and `lib/` of the
host's files on the web (the page's `libs` option names each file's
URL): all in one place, so two plugins with a file of the same name for
a platform don't build. A desktop build that ships any builds with `asuka_bundled`.
A plugin builds its libraries with its own tool (they need what they
need: SQLite, Xcode, the NDK and Zig), which writes each with
`plugin.StoreBuild` and the hash of the plugin's sources (the manifest's
`sources`); `asuka plugin publish` uploads them, refusing a build older
than its sources, downloads each back to check it, and records them in
the manifest.

A plugin's name, which goes in those paths, is letters, digits, `-`, `_` and `.`; two of an app's plugins can't
share one.

### SQLite

`asuka/sdk/sqlite` is a plugin: SQLite's own C, one pinned release
built with the same options for every platform by
`sdk/sqlite/internal/build` (Xcode, the NDK, Zig). With it, one library of Asuka's
(`sdk/sqlite/csrc/asq.c`) and one entry point: `asq_call(op, conn, arg, in,
inLen, out, outCap)`, every argument pointer-sized. A call is a whole
statement: its SQL and arguments in, its answer (a query's columns and
first rows, as many as fit the buffer) out; `next` brings the rest a
chunk at a time, and an answer too long for the buffer is kept for the
caller to take with a bigger one. Statements are cached by SQL in C
(64 per connection). The tables a call's commits wrote (the authorizer,
the preupdate and commit hooks) and its queries read come back with
each answer, so the live queries need no callbacks from C into Go.

Go crosses into C once per call, never per column, which is what makes
purego as fast as cgo here (a crossing costs more than a row): macOS,
Linux, iOS and Android load the library with `dlopen` through purego
(the mobile build modes' cgo is purego's way to `dlopen`, not
Asuka's), Windows with `LoadLibrary` through `syscall`, and the web
calls the same C built to WebAssembly from JavaScript. Every symbol but
`asq_call` and `asq_version` is hidden, so a system SQLite in the same
process (GTK may load one) never clashes. The CLI ships it with the app
as any plugin's library ([Plugins](#plugins)).

A database has one writer connection and, for a file, up to three
readers (write-ahead logging), handed out by the Go side. Writes take
turns on the writer: a semaphore that times out as SQLite's busy timeout
does, so a write inside another's transaction fails with `ErrBusy`
rather than hanging. Rows scan into structs by a plan made once per
type (field offsets and kinds), written in place; no reflection per row.

## iOS and Android: the core in the app

The phones' hosts link the C core (`core/`, built by `internal/cbuild`
with Xcode's clang and the NDK's) and call its ABI
(`core/include/asuka.h`, copied to `platform/ios/Sources/CAsuka/include`):

```c
void asuka_start(asuka_host_callbacks*);  // apply, measure, post, the native calls
int  asuka_load(const uint8_t*, size_t, char **err);  // bytecode: the dev player
int  asuka_run(char **err);               // the app compiled into the library
void asuka_event(const uint8_t*, size_t);
int64_t asuka_list_count(uint32_t list);
void asuka_list_bind(uint32_t list, int64_t index, uint32_t cell);
void asuka_list_unbind(uint32_t list, uint32_t cell);
void asuka_run_posted(void);              // the host's main-thread hop
```

All calls come on the main thread. The core's `post` calls the host's
callback, which hops with `DispatchQueue.main.async` / `Handler.post`
and calls `asuka_run_posted`.

- iOS: the core, with the app compiled to C (lang/cgen) for a release
  or without it for `asuka dev`'s player, is a static library in an
  XCFramework; `platform/ios` is a Swift package with the decoder
  (`AsukaOps`), the UIKit host (`AsukaHost`) and the C header
  (`CAsuka`); `App/` holds the `@main` app delegate. The CLI embeds
  them.
- Android: the core is `libasuka.so` per ABI (arm64-v8a, armeabi-v7a,
  x86_64), linked with 16 KB pages as new devices require;
  `core/src/abi/jni_android.c` is the JNI side of the ABI: Kotlin's
  `dev.asuka.AsukaNative` calls in; the core calls
  `AsukaRuntime.apply(ByteBuffer)` (a direct buffer over the encoded
  batch, one call per batch), `measure` and `post` back.
  `platform/android` is a Gradle project: the `asuka` library (decoder,
  Views host, JNI bindings) and `app`, the shell app the CLI builds per
  app (application ID and name passed as Gradle properties).

An app's plugins' libraries ([Plugins](#plugins)) ship with it: on iOS a
framework each in `Frameworks` (`asqlite.framework` for SQLite: an
Info.plist, a privacy manifest of its own, signed with the app;
`dlopen`ed by its path from the executable's directory, or
`../../Frameworks` from an extension); on Android each beside
`libasuka.so` in each ABI's `jniLibs`, loaded by name from the APK.

Crash reports (asuka/crash) are the core's record of the run
(`core/src/std/crash.c`, internal/crashrun and sdk/crash's in C, their
files' formats the same): the host begins it as the app starts
(`asuka_crash_start`, after `asuka_set_app_dir`, with JSON of the
device and the app; only for an app that reads its reports, which
lang/cgen marks, or the dev player), and the record keeps the run's
context in `crashes/runs/<run>.ctx` (breadcrumbs, values, foreground,
written as they change) and the core's own fatal error in its `.crash`
(`ak_panic`'s message and the VM's stack), locked (flock) while the run
lives. What only the host sees it writes in `crashes/inbox` (iOS: an
uncaught NSException with its frames' binaries, MetricKit's payloads;
Android: an uncaught JVM exception with every thread's stack, how the
last runs ended from `getHistoricalProcessExitReasons` with a
tombstone or an ANR's traces) and says so (`asuka_crash_notify`).
`crash.reports()` collects in a spawn (it reads files): ended runs'
fatal errors and the inbox become reports with the context of the run
they were in, each once, kept until the app deletes them; what the
system reports again of a crash already reported is left out.

## CLI (`cmd/asuka`)

```
asuka new <dir>                             a project from the template
asuka dev --platform ios|android <dir>       build, run, rebuild and restart on save
asuka build --platform ios|android <dir>     a release build for the stores
asuka publish --platform ios|android <dir>   the release build, uploaded: TestFlight, App Review; a Play track
asuka dev --platform mac <dir>               build and run on this Mac, rerun on save
asuka build --platform mac <dir>             a universal .app, signed (--sign; default ad hoc), its profile embedded (--profile); --appstore: a .pkg
asuka dev --platform windows <dir>           build and run on this PC, rerun on save
asuka build --platform windows <dir>         a folder: the .exe (icon, manifest, version; signed with ASUKA_WINDOWS_CERT) and its assets (--arch arm64)
asuka dev --platform web <dir>               serve it to the browser, rebuild and reload on save
asuka build --platform web <dir>             a static site: WebAssembly, the web host, a manifest
asuka dev|build <dir>                       no --platform: this computer's, on macOS (mac) and Windows (windows)
asuka generate <dir>                        the typed functions of the app's catalogs
asuka i18n init|add|check                   start, add languages to, and check the catalogs
asuka check|fmt|test|run [paths]            the compiler's errors; formatting; test blocks on the fake host; one file
asuka add|remove|install                     packages from the npm registry (asuka.lock)
asuka native new|add|generate|check          native packages and their glue
asuka lsp                                    the language server, on stdio
asuka publish --platform mac|windows|linux   the desktop build, its signed update (dist/updates), uploaded: a GitHub release, S3
asuka update keygen                         the Ed25519 key pair that signs desktop updates
```

**An app's metadata** is `asuka.toml` beside its Asuka code (TOML 1.0, read
by `internal/toml`), all optional:
name, ID (bundle ID and application ID), version and build number, a
1024×1024 icon, orientations (the phone's: an iPad or a large Android
screen turns the app any way, for split screen), the iOS team (and
whether it's iPhone only), provisioning profile, usage
descriptions and extra Info.plist keys, the Android permissions and
upload key. `asuka dev` and `asuka build` read it alike. An error names
its line and column.

**The accent color** (`accent = "#7c3aed"`) is the one color an app's
brand sets on native controls; the controls are the platform's, so
Asuka has no theme of its own over them (a style changes one view). It
travels with the app's code, not the shell: the compiler builds it into
the module's init, and each root, as it mounts, sends the `Accent`
command before any view is made. That way the dev player, which runs
any app, takes each one's, and drops it between apps. On iOS it's the
windows' tint color, which UIKit's controls and `.accent` take. On Android it's the seed of a Material 3 color scheme
(`SchemeContent`, light and dark, as Material's `DynamicColors` makes
it) on Android 12 and later; before that, Material's baseline colors,
with Asuka's own uses of the accent (links, `.accent`) taking the color
itself. `android.dynamicColor = true` takes the wallpaper's colors
instead, where the system has them. The theme context is made again
and the views that resolved theme colors are rebuilt, as on a dark mode
change.

**`asuka build --platform ios`** makes an `.ipa`, still without an Xcode project:
the host compiled optimized (no debug automation), the core with the
app compiled to C (lang/cgen) for `arm64` devices, linked with dead
stripping; an Info.plist with the
keys App Store validation checks (`DT*` from the SDK and Xcode,
`ITSAppUsesNonExemptEncryption`); the icon as an asset catalog compiled
by `actool`; then signing as Xcode would: the installed provisioning
profile that fits the app ID best (exact before prefix before `*`) for
the distribution (`app-store` by default, `ad-hoc`, `development`),
unexpired, whose certificate's identity is in the keychain; its
entitlements, with a wildcard app ID made the app's; `codesign` with
the identity's SHA-1, verified. `--simulator` makes the same release build
for the simulator.

**`asuka build --platform android`** makes an `.aab` for Google Play (no
`.apk`: Play makes each device's from it, with only that device's ABI):
the core with the app compiled to C as a stripped `libasuka.so` for
arm64-v8a, armeabi-v7a and x86_64,
the assets, launcher icons (legacy per density, and adaptive: the icon
inside the safe zone over its corner color), the permissions, the
version; Kotlin shrunk by R8 (the classes JNI and reflection reach are
kept); signed with the upload key named in `asuka.toml` or `ASUKA_KEYSTORE`,
its passwords from the environment (never on a command line). A
release build never loads a library from the app's files, as debug
builds do in development; cleartext HTTP (the player's assets) is
debug-only, as is iOS's local networking exception.

**`asuka publish --platform ios`** and **`--platform android`** make the release build and
upload it, through the stores' APIs from the CLI (no altool, no Gradle
plugin), so a release is one command, in CI too. Everything the store
can refuse is asked first, before the build: the app (App Store Connect
by bundle ID; Play by package, in an edit opened to look and deleted),
whether the version still takes builds (approved or released: no), the
TestFlight groups by name, and the store's highest build number. The
build is numbered one above it (and at least `asuka.toml`'s `build`), so
`build` needn't be raised by hand; `--build` sets one. iOS: a JWT signed
with the App Store Connect API key (ES256, renewed every 10 minutes);
the `.ipa` sent as `buildUploads` says, in parts, a few at a time, each
retried; then the upload and the build polled until Apple has processed
it (its warnings and errors printed), What to Test set, the build given
to the groups (an internal group with every build skipped; an external
one sends it to beta review), and with `--review` the App Store version
made or found, given the build, its What's New (each language's, which
an update needs) and submitted. Android: an OAuth token from the service
account's signed assertion (RS256); the `.aab` in a resumable upload,
chunk by chunk, resumed from where Play says it got to; a release on the
track (`completed`, `inProgress` with `--rollout`, or a draft, which an
app never published takes only); the edit committed without cancelling
changes in review unless asked, and, if Play holds changes for the
Console, committed unsent. Requests are retried when rate limited
(`Retry-After`) and, where safe to repeat, when the server or the
network fails.

**`asuka dev` runs the app in a player**: the platform's host with the
core and no app in it (the simulator's, an emulator's or a phone's
shell app, a desktop host, a page), built with the app's ID, name, icon
and native packages, and built and installed again only when those
change. The CLI compiles the app to bytecode and sends it to the
players that connect (internal/devserver, core/src/abi/dev.h: framed
modules one way; hello, logs, how each reload went and errors the
other); on each save it compiles again, in milliseconds, and sends the
new module, which each player loads in place:

- **State survives a reload** without code, as React's Fast Refresh
  keeps hooks' state: every state cell is made with a key, its
  component's path and its name (core/src/vm/keep.h). Before the old
  module goes, each live cell's value is saved in a form that names its
  types, fields and cases; the new module's cells with the same keys
  start from it, converted to the new types by name. A value that can't
  be converted (a field added whose type has no zero value, a type
  that's now another kind) starts from its initial value, as does a
  cell whose component moved. The views that are still there are kept,
  so a reload changes only what the edit changed. The player
  reports how many cells it kept and how long the load took.
- **Noticing a change**: the system says when a file under the app's
  directory changes (`internal/fswatch`: FSEvents, inotify,
  ReadDirectoryChangesW; 25 ms for a burst to end); where the system
  can't watch, `asuka dev` polls. A module that doesn't compile is
  printed and the players keep the last one; the next that does goes
  out.
- **Several players at once**: each connection gets every module (the
  app on an iPhone and an Android phone together, and the desktop).
  The player's logs come back to the CLI, which prints them.
- **Reaching the CLI**: the simulator shares the Mac's loopback; an
  Android device or emulator through `adb reverse` (its loopback reaches
  the computer's, on the same port); an iPhone through the cable, where
  the Mac connects to the player (usbmuxd reaches a port on the phone's
  loopback, `asuka_dev_listen`; the phone can't open one the other way),
  else over the local network (Local Network permission: the player
  tries each of the Mac's addresses, on a port kept the same from run to
  run, and keeps them, so one launched again from the home screen finds
  it); a desktop player on loopback; the web's page through the dev
  server, which serves the page and the modules (`asuka_dev_reload`). A
  player dials again whenever its session ends, so it comes back to an
  `asuka dev` that restarted; a connection the Mac makes takes over from
  the player's own.
- **Signing for an iPhone**: the player's App ID is the team's own
  (`dev.asuka.player.<team>`). A development profile that fits (the
  device among its devices, the capabilities the build's entitlements
  need) is used as it is; else `asuka dev` writes an Xcode project of
  one empty app per bundle ID, with those entitlements, and has
  `xcodebuild -allowProvisioningUpdates
  -allowProvisioningDeviceRegistration` make the profiles (the Xcode
  account's, or the App Store Connect key's), once a session. The
  player goes without the capabilities its team can't have.
- **Native code**: the app's native packages' Swift, Kotlin, JavaScript
  or Go is built into its player, so a change to it rebuilds and
  relaunches the player (generating the glue first); a change to Asuka
  code reloads. `--once` builds and launches once, without watching.

## Repository layout

```
asuka/
  core/              the C core: base, signal, layout, ops, ui (the UI
                     runtime), vm (bytecode, tasks), rt (values, the
                     scheduler, streams, JSON), native, abi; host/fake
                     (the test host) and runner (asuka-run)
  lang/              the Asuka compiler: syntax, check, ir, bytecode, cgen
                     (bytecode to C), prelude (the elements), compile
                     (the driver), lsp (the language server)
  std/               the standard library's packages (asuka/<name>)
  ops/               the wire's schema; go generate writes the C, Swift,
                     Kotlin and JavaScript codecs
  layout/            the layout's types and Chrome's fixtures (the C
                     layout's conformance)
  i18n/              languages, catalogs, ICU messages, CLDR's formats
                     (compiled into what the core says)
  host/              the Go macOS host: darwin (AppKit through purego);
                     bridge (the core's ABI through purego); apphost
                     (cmd/asuka-host); hosttest (its tests on the
                     core); the Host interface
  native/            native packages' Go runtime (Go apps' packages)
  internal/nativegen/ asuka native's generator (Swift, Kotlin,
                     JavaScript, C; Go for Go apps' packages)
  platform/ios/      the Swift host and decoder
  platform/android/  the Kotlin host and decoder (Gradle)
  platform/web/      the JavaScript host and decoder; the core runs in the
                     page as WebAssembly
  platform/macos/    the Swift runtime packages' Swift builds against
  platform/windows/  the Windows host, C17 on Win32 (src/), its tests
                     (Go, driving apps in Windows) and portable C tests
  platform/linux/    the Linux host, C17 on GTK 4 and libadwaita opened
                     with dlopen (src/), its tests (Go, driving apps
                     under Xvfb) and fake D-Bus services
  platform/desktop/  what the C desktop hosts share: asuka_desktop.h, the
                     native registry, packages' shared C (fetch, COM,
                     GLib, WebView2), its tests' test host; and desktop.h,
                     the hosts' own (JSON, files, the app's settings and
                     directories, window and route state, flyouts'
                     places, Asuka's own words, animated images)
  cmd/asuka/         the CLI; cmd/asuka-host: the Go macOS host's executable
  internal/          the CLI's parts: cbuild (building the core),
                     appbuild, devserver, pkgs (packages), boot, ...
  plugin/            plugins' manifests, fetching and linking
  template/          what `asuka new` copies
  examples/          apps in Asuka
  conformance/       what every host draws for the style props, measured
  bench/             Asuka against SwiftUI and Compose
  e2e/               GUI tests on the simulator, the emulator, the
                     desktops and headless Chrome
```

## Testing

- The core (`go test ./core`): C tests under ASan and UBSan, a file per
  area (`core/test/*_test.c`): signals (glitch-freedom, laziness,
  disposal, batching), layout (Chrome's results on Taffy's CSS fixtures,
  measures and allocations counted), ops (byte-identical to the golden
  batches), the UI runtime on the fake host (op streams: one write to
  state a Text reads gives one SetProp; a list of 10,000 items binds
  only what shows; a popped screen disposes every owner it made;
  steady-state flushes allocate nothing, `ak_alloc_count`), the VM and
  the release backend (every module run on both, which must agree).
- The compiler (`go test ./lang/...`): the parser and printer, the
  checker's diagnostics (`testdata/errors`, `// ERROR` comments), and
  programs (`lang/compile/testdata/run` and `ui`) run on the VM and
  compiled to C, which must print the same: the language's conformance
  suite, UI programs driven on the fake host included.
- Apps: `test` blocks (`asuka test`) mount views on the fake host and
  drive them by what the screen shows (`tap`, `type`, `expect`), as a
  user would; the clock moves only when nothing else can run, so a
  `sleep(5s)` takes no time and nothing waits on wall-clock time.
- The macOS host (`go test ./host/darwin`): host/hosttest compiles an
  Asuka app, runs it on the core in the test's process, and the test
  drives and checks the host's own views (a click through the button's
  action, the text set again, frames, list binding, disposal).
- The Windows host (`go test ./platform/windows`): each test compiles an
  Asuka app to C, builds it into one executable with Zig, and runs it in
  Windows (on Windows, or in the swiftvm VM `ASUKA_WINDOWS_VM` names,
  through a scheduled task in the signed-in user's session, since a
  window needs a desktop) with `ASUKA_SCRIPT` steps, then checks the
  view tree each `snap:` step dumps and the PNGs (a click through the
  button's action, a sheet's form, list rows bound, rich text typed,
  overlays, drawing by pixel, drops, UI Automation read by a client).
  `TestPortable` runs the host's C that needs no Windows (RTF) here,
  under ASan and UBSan; `go test ./platform/desktop` the shared C.
- The Linux host (`go test ./platform/linux` under `xvfb-run -a
  dbus-run-session`): likewise, its apps built into the executable an
  app ships and driven in real GTK windows (`ASUKA_SCRIPT`), the views
  read back (`ASUKA_DUMP`), with fake D-Bus services for notifications
  and the tray.
- `ops`: the C encoder and the generated Swift, Kotlin and JavaScript
  decoders round-trip the same golden batches (`swift test --package-path
  platform/ios`; `./gradlew :asuka:testDebugUnitTest` in `platform/android`;
  `node --test platform/web`).
- The CLI (`go test ./cmd/asuka ./internal/...`): builds for every
  platform, the dev loop's reloads keeping state (on the Mac, the
  simulator, the emulator and headless Chrome), an app with standard
  library packages built and run on the Mac (`TestBuildMacStd`).
- Hosts end to end (`e2e/`, build tag `e2e`): the examples built and
  driven through the native views on the simulator, the emulator, the
  desktops and headless Chrome. Debug builds take automation commands
  (tap, type, submit, back, tab, rotate, dump: what the screen shows),
  so every platform runs the same test.
- Rendering (`conformance`): each host draws one sheet of cases (a
  background's edges, rounded corners, borders, opacity and group
  opacity, clips, shadows, gradients, transforms), written in Asuka,
  between two registration markers; its runner captures the screen, and
  the cases measure the pixels: where an edge is to the device pixel, a
  shadow's Gaussian σ, a color's mix, with tolerances, not against a
  reference, so one set holds for every host at any scale. Runners:
  `host/darwin` (Core Animation's own renderer into a Metal texture),
  the Windows host (`PrintWindow`), and `e2e` on the simulator, the
  emulator and headless Chrome at 1×, 1.5× and 2×.
- Tests never pass or fail on wall-clock time: wait for the condition,
  count the work.

## Milestones

Asuka was first a Go framework: apps in Go on a Go core, which reached
every platform this document describes (iOS and Android, then the
desktops and the web) with the hosts, the wire, the layout and the CLI
that remain. RFC 0007 replaced the app language with Asuka and the core
with C; its milestones are the rewrite's, each ending with its tests
green:

- **Done**: the language and its compiler (lang/: front end, checker,
  bytecode, the VM, the C backend, both passing one conformance suite);
  the core in C (signals, layout at Chrome's parity, ops byte-identical
  to the golden files, the UI runtime, tasks and streams); iOS, Android
  and the web on it, with `asuka dev`'s bytecode reloads keeping state;
  the desktop hosts on it (Windows in C, the Mac and Linux through
  host/bridge); packages from the npm
  registry; the standard library (`std/`).
- **In progress** (RFC 0007's E1, N1 and X): every element of the
  schema in the UI runtime and the examples in Asuka; the Go SDK's
  packages as standard library packages (native packages and `native
  fn`s); the Go core and Go apps removed, and the docs (with `zh` and
  `ja`) on the new Asuka.

Later, past the rewrite: what each platform still lacks is listed in
its section ("Desktop, later", "The web"), and in docs/parity.md
against SwiftUI and Expo.

## Rich text

Platform labels can't express everything a rich text model needs: span
backgrounds shaped per line (radius, box-decoration-break), inline
components that wrap with the line, selection that crosses paragraphs
(markdown, chat transcripts), and cheap updates to long streaming text
(an append re-typesets the whole string on the main thread).
`RichText` owns its text model and its view (core/src/ui/rich.c); `Text`
stays a platform label, with native accessibility, Dynamic Type and
behavior.

**Built:**

- The model is a `[Span]` (text, font fields that default to the
  view's, italic, monospaced, color, underline, strikethrough, a
  background, a link, an inline component); a `"\n"` starts a
  paragraph. The view's style (font, color, textAlign, lines) is the
  spans' default, through the common text props.
- On the wire it's one prop: a table of the spans' styles, then the
  paragraphs' runs, each a style index and text. The core sends it only
  when the encoding changed.
- Each host typesets with its platform's engine (CoreText's typesetter
  on iOS, `StaticLayout` on Android) and draws in a view of Asuka's own
  exactly the layout it measured: backgrounds per line fragment with
  rounded corners (fitted to the text, not past a line's end), then the
  text, then underlines and strikethroughs (CoreText draws neither).
  Line limits truncate the last line with an ellipsis; Android takes
  the text appearance's line height, as `TextView` does.
- Layouts are cached per paragraph by its bytes (runs and their
  styles), the view's text props, the width and the environment (text
  size, dark mode), so appending to a streaming reply typesets only the
  paragraph being written.
- Dynamic Type: the core measures as soon as the text size changes,
  before UIKit hands views their new traits, so iOS measures and draws
  with the app's content size category and the view's other traits.
- Links are hit-tested on the typeset lines and reported as `Link`.

- Links are accessibility elements of their own, so a screen reader
  finds and activates each: `UIAccessibilityElement`s on iOS (the text
  becomes a container of the whole text and its links),
  `ExploreByTouchHelper` virtual views on Android.
- Layouts are prepared off the main thread. When a text's content
  changes, the paragraphs not in the cache start typesetting on
  background threads (CoreText's typesetter and `StaticLayout` are
  thread-safe) at the width the view was last laid out at. The main
  thread, measuring moments later, takes a layout that's done, waits for
  one in flight (the rest of its work, not all of it), or typesets it
  itself. The attributed or spanned strings are built on the main thread
  (they read the view's tint and theme); the line breaking, the work,
  isn't. Texts set in one flush typeset in parallel.
- Inline components: a span's `inline: Inline(key:, build:)` is a component
  (an image, a badge, a button), sitting on the baseline and wrapping
  with the words. It's built once for its key, a child view of the
  RichText and a layout root of its own, laid out at its max-content
  size (at most the screen's width). In the text it's U+FFFC with a flag.
  Its size has to reach the host before the host measures the text, and
  the host can measure the component's leaves only once their props are
  applied. So a flush lays out inline components after applying the
  props and before the layout that measures the texts. When a size
  changed, the sizes (`RichText`'s `Inlines`: node ids and sizes) go out in
  a crossing of their own; otherwise there's none. The host reserves
  each placeholder (a `CTRunDelegate`, a `ReplacementSpan`) and puts the
  child there: its frame's size is the core's, its origin the text's.
- `Selectable { … }` makes the RichTexts inside it one text (joined by
  newlines) with one selection, as the platform selects:
  - iOS: `UITextInteraction` (loupe, handles, the edit menu with Copy and
    Select All) on a `UITextInput` overlay that answers with the texts'
    geometry. The overlay is hidden from VoiceOver, so the texts and
    links stay accessible. Touches on a text in a scroll view within
    (a code block) are the scroll view's.
  - Android: as a `TextView` selects. A long press selects a word and a
    drag goes on word by word. The theme's handles move the ends, which
    never cross. The floating action mode has Copy and Select all, and a
    tap clears. Each text draws its part of the selection in the theme's
    highlight color. A scroll view's taking the gesture over cancels the
    long press.
  - Copying leaves out the inline components.
- `asuka/markdown` (`markdown.View(text) { streaming: true }`) is each
  platform's own Markdown, so a document reads, selects and speaks as the
  platform's text does: `AttributedString` in a text view on iOS and the
  Mac, Markwon on Android, a vendored marked on the web (raw HTML escaped,
  only web, mail and relative links kept), and on Windows and Linux the
  package's own parser written as RTF into a rich edit and as Pango
  markup in a label. While a reply streams in, its text is healed once,
  in Asuka, before it reaches any platform (`markdown.heal`: an unclosed
  emphasis, a link being written), so a half-written document renders as
  it will. Links open with the platform.

**Not yet:** selection for screen reader users (VoiceOver and TalkBack
read the texts and links but don't select across them).

## Languages

An app says its messages in the language of the people who use it, and is
laid out the way that language reads. docs/i18n.md is the guide; this is how
it's built. Package `i18n` and the catalog generator (`internal/i18ngen`) are
MyGo's, whose data comes from a pinned release of CLDR.

- **Catalogs** are ARB files of ICU MessageFormat, one per language, in
  `locales/`. The compiler reads them (lang/compile: internal/i18ngen checks
  them, a problem a diagnostic at its file and line) and gives the app `tr`:
  `tr.unread(count: n)` is checked against the source's key and its
  placeholders' types. Package i18n compiles them (`Catalog.CompileWith`) into
  a blob the module carries: each message, per language, a small program of
  text, arguments, plurals, selects, numbers and dates, with what the core
  needs of CLDR for those languages only (their number symbols and patterns;
  their dates' names, and every style's and skeleton's pattern in 12 and 24
  hours, resolved at compile time so the core needs no pattern generator).
  Nothing is generated into the app's directory.
- **The core says them** (core/src/ui/i18n.c, a port of `Catalog.Format`):
  the key's message in the best language the catalog has for the one shown,
  falling back along CLDR's parent locales to the source; CLDR's plural rules
  (i18n_gen.c, generated from package i18n's tables); numbers and dates in
  the shown language's formats, dates in the device's zone and the user's
  hour cycle; the pseudo locales. A test says over 900 cases of Go's Format
  the same way, and every CLDR sample of every language's plural rules.
- **The language shown** is the process's (core/src/ui/locale.c, as
  `ui/locale.go` was): the environment's languages (or what `setLocale`
  chose, first) matched against the catalog's languages by CLDR's distances,
  else the app's own: a reader of Chinese sees an English-only app in
  English, as on UIKit. It's a signal: `i18n.say` reads it, so a binding that
  says a message follows the language as it follows its arguments, and a
  change costs the bindings that said one. The checker counts a message as
  reading state, so a function that says one is followed where a binding
  calls it, and `spawn` can't say one (the language is the main thread's).
  Said in a handler, a message is what it said then, which is what an alert
  wants. A change of direction lays the app out again, and the host hears
  `Locale`.
- **Regional formats** are a setting of their own: the environment's `Locale`,
  `FirstWeekday` and `HourCycle`, with `i18n.formatLocale()` putting them
  together with the language (`en-GB-u-fw-mon-hc-h23`), as Apple's
  `Locale.current` does.
- **Asuka's own words** (`internal/l10n`) are the few the platforms don't supply:
  an alert's only button, a search field's placeholder, a desktop back button.
  They're a library's catalog an app overrides with keys `asuka.<key>`.

**Right to left.** The layout engine implements CSS `direction`, against
Chrome's results on Taffy's fixtures. The core sets it on each layout root
(the root, screens, sheets, cells, inline components); the rest inherit.
`Row`, `Justify` and `Align` follow it. `Edges.Left` and `Right` are physical,
as in CSS; `Start` and `End` mirror. Frames are always physical, so **a host
never mirrors what the core places**: it only mirrors what it draws itself.

What every host does:

- **Reports** `Languages` (BCP 47, most preferred first, the app's own language
  first if the system or `SaveLocale` keeps one), `Locale`, `FirstWeekday`
  and `HourCycle`, again as they change.
- **`Locale`** (a command, as the root mounts and when it changes; flags, then
  the tag): the platform's views go that way, whatever the system's language,
  for what they lay out themselves: a navigation bar and its back button, push
  and pop transitions, the tab bar, a list's swipe actions, a text's natural
  alignment (`TextStart` is the side the language starts on, and is the default),
  a slider's direction, `RichText`'s paragraphs. Where the platform lets it, it
  also says the platform's own words in the language.
- **`LayoutDirection`** (a common prop): the same for one view and what's
  in it, for a view's `layoutDirection: .leftToRight`.
- **A horizontal `Scroll` or `List`** starts at its trailing end right to left,
  and keeps its distance from that end as its content's width changes. The
  content is at the scroll view's origin, laid out from the right within it.
  `OnScroll` and `ScrollToOffset` stay physical.
- **Symbols** whose names say a direction by `forward` or `backward` mirror
  (`chevron.forward`); `chevron.left` doesn't, since it means left.
- **`SaveLocale`** (the command `setLocale` sends) keeps the language: AppleLanguages of the app's defaults
  (iOS, macOS), which Settings shows and UIKit and AppKit read at launch;
  `LocaleManager` (Android), or the host's own preferences on Windows. Android declares its languages in `locales_config`, iOS and macOS
  in `CFBundleLocalizations` and `.lproj`s: `asuka build` writes them from the
  catalogs, with the app's name and purpose strings in each.

Under `asuka dev` the player runs every app: `--lang` and `--dir` force a
language and a direction for the run through the module's init
(`i18n.force`), and a choice `setLocale` saves there is the player's.

What each host does by its own means:

- **iOS**: `semanticContentAttribute` through the appearance and a walk of what
  exists, with a window trait override, since the back chevron follows traits
  (iOS 17 and later). The walk waits until the core has returned: a collection view
  asks for its cells as its direction changes. UIKit's own words stay in the
  process's language until the next launch.
- **Android**: the themed context carries the locale and direction, views are
  made again as for a dark mode change, and `AsukaActivity` overrides
  `getResources` and `getTheme` for Material's pickers, which are `DialogFragment`s
  on the Activity's context. It takes no recreation.
- **macOS**: `setUserInterfaceLayoutDirection:` per view. `NSApplication`'s
  direction is read once, so the host sets `NSForceRightToLeftWritingDirection` in
  the argument domain before it makes the app, from the language it will show. A
  change of direction while running leaves the traffic lights, the title, a
  vertical scroller's side and an alert's button order until the next launch. The
  standard menus' words come from AppKit's own tables.
- **Windows**: `WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT` on top-level windows only.
  Each has one child that fills its client area, so below it every window and DC
  stays left to right, and frames stay physical, while Windows mirrors the title
  and menu bars. The date picker, which isn't custom drawn, doesn't mirror.

A line of text reads as its own letters do (its first strong character), and its
side comes from the view's direction, on every host; only a `WithDirection` forces
a reading order. So a message in a right-to-left language has to say how it starts
when an argument or a number comes first: `i18n` then begins it with a
right-to-left mark (CLDR's Arabic percent begins with a left-to-right one).

## Desktop, later

On the same core, `ops` and app code as the phones:

| Platform | Host | Notes |
|---|---|---|
| macOS | Go, AppKit through purego (`host/darwin`) | No cgo. `internal/darwin` is MyGo's Objective-C runtime layer (classes made at runtime, blocks, `WithPool`, a main run loop source for `Post`). |
| Windows | C17, Win32 with common controls v6 (`platform/windows`) | The core and the app linked into one executable. GDI+ through its flat API, DWM, uxtheme, Direct3D 11, WebView2, UI Automation and WinRT through their COM vtables in C. Not WinUI 3: it needs the Windows App SDK beside the app, and XAML is a second reconciler. `List` is core-virtualized: no Win32 list hosts arbitrary rows. |
| Linux | C, GTK 4 and libadwaita opened with dlopen (`platform/linux`) | One executable: the host, the core and the app compiled to C; its own declarations of GTK 4.14, libadwaita 1.5 and the GLib stack (Ubuntu 24.04, Debian 13, Fedora 40 and later), shared with packages' C (`asuka_linux.h`), so it builds from any system with Zig. libadwaita, which every GNOME app links, gives navigation, dialogs and the header bar, and the look an app that sets no style should have. |

Desktop hosts run in the app's process. On macOS one executable for
every app (cmd/asuka-host, with the app's native packages' Go linked in
when it has any) locks the main OS thread and runs the platform's event
loop there, opens the core (`libasuka`, the app compiled in for a
release) through purego (host/bridge), and decodes each batch with
`ops`' Go decoder, in place. On Windows and Linux the host is C (RFC
0008): the host, the C core and the app compiled to C (lang/cgen) are
one executable per app, the host decoding each batch in place with
the core's own reader (`core/src/ops`); `asuka dev`'s player is the host
and the core without an app, which loads the bytecode the CLI sends
(`asuka_dev_connect`). Either way the core's callbacks are made once and
routed by user data, never per view.

How the macOS host maps Asuka (`host/darwin`):

- One window holds the root, its content a flipped view; the
  environment is the content's size, the display's scale, dark mode,
  Reduce Motion, locale and time zone (no safe area, keyboard or text
  size). Frames are set directly: AppKit's coordinates are flipped to
  Asuka's (top left) by flipped container views.
- Controls are AppKit's: `NSTextField` (labels and fields; secure ones
  swap to `NSSecureTextField`, multiline ones to an `NSTextView`),
  `NSButton` (Plain is borderless text in the accent color, Filled the
  accent bezel), `NSSwitch`, `NSSlider`, `NSStepper`,
  `NSSegmentedControl`, `NSPopUpButton` for `Wheel` and `Picker`, `NSDatePicker`
  (its calendar and clock in place for `Inline`),
  `NSColorWell`, `NSProgressIndicator`, `NSImageView`, `NSScrollView`.
  Menus are `NSMenu`s: a button's pops up, a view's `LongPress` menu is
  its context menu (a secondary click). A secondary click is also a
  long press. A swipe is a pan that ends fast (a drag with the mouse), a
  trackpad's two-finger flick (across always; up and down where no
  scroll view around scrolls that way, the scroll still going to it), or
  a three-finger swipe where the system leaves those to apps.
- A `Stack` shows its top screen and slides on push and pop; the
  window's toolbar belongs to the innermost stack on screen (a back
  button, the top screen's bar buttons and search field) and its title
  is the screen's. `Tabs` is an `NSTabView` (a tab's badge in its label,
  "Inbox (3)", since it draws its tabs' labels alone; VoiceOver hears
  "Inbox, 3 items"); a `Sheet` a sheet window
  (Escape dismisses it); an `Alert` an `NSAlert` shown as a sheet (its
  fields `NSTextField`s, the accessory view, return pressing the default
  button); a
  `Snackbar` an overlay at the bottom of the window's content (an
  `NSVisualEffectView`, its time stopped under the mouse, announced
  through `NSAccessibilityPostNotificationWithUserInfo`).
- `List` is core-virtualized, as on Windows: a scroll view whose rows
  the host places, binds through `ListBind` and recycles as it
  scrolls, heights from the cells' frames (estimated until bound).
  `NSTableView` would want row heights from a callback returning a
  float, which purego can't make.
- `WebView` is a `WKWebView`, as on iOS: `Block` a content rule list
  (the page loads once it's compiled, so its first requests are blocked
  too), a response it can't show a `Download`, its state observed (KVO)
  and reported once a turn; the `web` module runs script for its value
  (`callAsyncJavaScript`) and reads the cookies, the `js` module runs
  `asuka/js`'s scripts in a web view that's never shown.
- `RichText` is a non-editable `NSTextView` (TextKit lays it out and
  draws it, links, selection and accessibility come with it); outside
  a `Selectable` it takes clicks only on links. Inline components are
  attachments of their size, their views placed at the attachment.
- Messages with float or struct arguments go through purego's typed
  functions (`internal/darwin`'s `Msg*`), which use reflection: setting a
  frame allocates. The rest go through `SyscallN`.
- Pull to refresh: a trackpad's (or Magic Mouse's) scroll pulled past
  a Scroll's or List's top, as its elastic overscroll lets it, and let
  go of beyond 60 points; a spinner above the content (the scroll view's
  top content inset) while it refreshes. Matched views move the old view
  into its window's content view and the frames carry the motion.
- Native packages: their Swift is built into the app's Swift library
  (`host/darwin/swift.go` loads it through purego and calls its
  `@_cdecl` entry points with `SyscallN`). A package's Go view
  (`implementT`) registers with package `native`: an `NSView` made
  through purego's objc package, put in the node's view and filling
  it; on Windows a child window made in the context's `Parent`, its
  controls' notifications given to it.
- Visual props are the views' layers'. The layers under the window's
  flipped content view have y down: gradients' points, shadows' offsets
  and transforms are given so. AppKit owns a layer-backed view's
  layer's transform and shadow: it sets the transform to the identity
  as the window displays, so a view's `transform` is held by an animation
  that never ends (from what's drawn, so `animate { }` animates it), and
  it sets the shadow from the view's own (none) as the view moves into
  a window, so the host sets it again then. A shadow is cast by the
  view's shape (`shadowPath`), as on iOS; a view that clips and casts
  one clips its children in a view of their own that fills it, as on
  iOS: a layer that masks to its bounds masks its own shadow too.
- The packages' modules are iOS's, the same methods and replies, through
  the Mac's frameworks (each file of `host/darwin` registers its own):
  AVFoundation for the camera (the view's layer is the capture session's
  preview; photo, movie and metadata outputs) and `AVAudioRecorder`,
  with no audio session; Contacts, EventKit and PhotoKit; Vision,
  NaturalLanguage and Speech, off the main thread; MapKit;
  AuthenticationServices; UserNotifications and the application
  delegate for APNs. What iOS shows that AppKit hasn't, the host shows
  itself: `camera.Take` is a sheet with a live preview, and
  `contacts.Add` and `calendar.Add` ask in an alert over the window, then
  save. A Mac has no back camera ("back" is the built-in one), no motion
  sensors, no widget extension; a task in the background holds an
  activity (App Nap off: a Mac app runs out of sight), periodic ones run
  while the app does; `PreventCapture` keeps its windows out of
  screenshots and recordings (`NSWindowSharingNone`) and `OnScreenshot`
  sees the files Spotlight finds (`kMDItemIsScreenCapture`).
- Windows' options are `NSWindow`'s, set as it opens (`winctl.go`):
  its style mask (no resize, close or minimize button), the zoom button
  turned off, the content's least and most size, the floating level for
  always on top, out of the Window menu for skipping the taskbar (a Mac
  has none), a background color, or a clear and non-opaque window for
  transparent. A hidden title bar is a full-size content view under a
  transparent, title-less title bar: the traffic lights stay where they
  are, and `SafeTop` is the content's height less its
  `contentLayoutRect`'s (a toolbar in the title bar adds its own). A
  child `Window` is a child window of its parent's (ordered above it,
  moving with it), a modal one a sheet of it. Places are the frame's top
  left from the main display's top left, y down, flipped from AppKit's.
  The actions are AppKit's (`orderOut:`, `makeKeyAndOrderFront:` with
  the app activated and unhidden for `Focus`, `miniaturize:`, `zoom:`,
  `toggleFullScreen:`, `center`, `setContentSize:`,
  `setFrameTopLeftPoint:`, `setLevel:`), and the state is read back from
  the window as its delegate hears it move, resize, miniaturize, become
  or stop being key, and enter or leave full screen, and as the app
  activates, hides or unhides: visible is shown or minimized (a Mac's
  `isVisible` is false for a minimized window), maximized is zoomed. A
  window's frame comes back with AppKit's frame autosave, under
  `asuka <app>` for the main window's key (where it was kept before
  keys) and `asuka <app> <key>` for the others', restored as it opens
  (`setFrameUsingName:`). The user closing a window the core asks about
  (`windowShouldClose:`) leaves it open and sends `CloseRequest`;
  `WindowAction` Close closes the main window past that question, and
  the app quits once its last window has.
- Debug automation: `ASUKA_SNAPSHOT=file.png` renders the window to a PNG
  once it has shown, without taking focus (`ASUKA_DUMP=1` writes the view
  tree beside it), then quits.

How the Windows host maps Asuka (`platform/windows`):

- The host is C17 (`platform/windows/src`, a file per part: `host.c` the
  nodes and batches, `views.c` and `controls.c` the views, `nav.c` the
  navigation, `window.c` and `winctl.c` the windows, `menu.c`,
  `list.c`, `rich.c`, `web.c`, `a11y.c`, `interaction.c` and so on;
  `host.h` and `parts.h` what they share), with the C shared with the
  Linux host in `platform/desktop` (`desktop.h`'s `dsk_` API: JSON,
  files, the app's settings and directories, window and route state,
  flyouts' places, Asuka's own words, animated images). It links the
  C core statically: a release is one executable, the app compiled to C
  (lang/cgen) in it, its icon, manifest and version a COFF object linked
  in; `asuka dev`'s player is the host and the core without an app, a
  console program that loads the bytecode the CLI sends
  (`asuka_dev_connect`). It decodes each batch in place with the core's
  own reader (`core/src/ops`: `ak_batch_next`, the payloads' decoders)
  and encodes events with its encoders.
- One thread: the core, every Win32 call and every state write run on
  the main thread, which runs the message loop. Posting (the core's
  `post` callback, `asuka_desktop.h`'s, a worker's result) appends to a
  queue under an SRW lock and posts one message to a message-only
  window, which takes the queue whole and runs it. One window procedure,
  registered for every window class of the host's, routes by window to
  its node (a map from `HWND`); controls are subclassed
  (`SetWindowSubclass`) with one procedure that finds the handler in the
  subclass's data. `IsDialogMessage` gives Tab and arrow key navigation.
  Calls into the core are counted: a control that would run a modal loop
  (a menu, a task dialog, the color dialog), or work that would enter
  the core again, waits until the core has returned, and so do the
  events raised meanwhile, sent in order then.
- Every real node is a child window. The app's window holds the root;
  the environment is its client area's size in points (DIPs), its DPI
  as the scale (per-monitor DPI awareness v2), the text size
  (Accessibility's `TextScaleFactor`, as FontScale), dark mode (the
  app mode in the registry; the title bar through DWM, menus and scroll
  bars through uxtheme's app mode), animations off as Reduce Motion,
  the locale, and the time zone as an IANA name (Windows's ICU).
  `Environment.Platform` is `PlatformWindows`: the environment's
  platform bits go from two to three (bits 2-4), which the mobile hosts,
  sending only two, still decode.
- Views draw themselves (a window class of the host's) into a buffered
  DC: first what's behind them where they aren't opaque (the parent and
  the siblings they overlap, drawn again into the DC, as
  `DrawThemeParentBackground` would but recursively), then the
  background, border, corner radius, gradient (GDI+, antialiased,
  blended in sRGB), then the content; text with GDI (`DrawText`, which
  also measures it, with the same font: min-content is the widest
  word). Views that take no mouse events are `HTTRANSPARENT`, so a
  click on a text reaches the pressable row under it. A pressable view
  shows Fluent's hover and pressed fills and takes Enter and Space.
- A `Shader` is its program as HLSL (compiled with the app, beside its
  GLSL, by `internal/shadercode`), compiled by Windows's compiler
  (`d3dcompiler_47`, loaded at run time) once per program and drawn with
  Direct3D 11 (the GPU's device, or WARP's), on a render thread of the
  host's, into a
  texture of the view's size in pixels that the GPU copies back to
  memory: the main thread paints it with GDI+ as it paints the views
  around it, and never waits for the GPU. Animated, a timer starts a
  frame whenever none is rendering.
- A view that clips with a corner radius is shaped by a window region
  (`SetWindowRgn`), which clips its child windows too: the pixels its
  rounded rectangle covers whole. Its corners' edge pixels, outside the
  region, are its parent's, which draws them antialiased: the view's
  tree drawn there and mixed with what's behind by how much of each
  pixel the rounded rectangle covers. A window in it painting over a
  corner has the parent draw that corner again.
- What a child window can't be, a picture draws: a view of the host's
  above the view, taking no clicks, that draws what's behind it, then
  the view's tree as it paints. A view with child windows at an opacity
  between 0 and 1 is drawn blended with what's behind it, as one
  picture (overlapping children don't show through each other); the
  view stays under the picture and takes clicks and the keyboard. A
  view without child windows fades its own drawing. (A layered child
  window can be translucent, but the windows in it aren't painted.) A
  rotated view is drawn turned (GDI+), its tree taken over black and
  over white for its coverage; its own window is shaped to nothing
  meanwhile, so it's drawn, not used. A picture is drawn again as
  what's in the view paints, or moves.
- `Style.Transform`'s translation moves the view's window; its scale
  sizes the window about its center and multiplies the scale of what's
  in it (`k`, as a window's DPI does: fonts, frames, controls and
  drawing at the scaled size, still windows that take clicks); a scale
  that differs between the axes sizes the window by each and what's in
  it by their geometric mean. They step with `animate { }`'s frames.
- A view's `material` is drawn as Fluent's acrylic in the view (DWM's
  backdrops are top-level windows' only): what's behind it, blurred
  (three box blurs, a Gaussian's likeness) and tinted light or dark. A
  view under it painting draws it again.
- Controls are the common controls, drawn as Windows 11 draws its own
  (WinUI's Fluent styles) through custom draw, so they look current in
  light and dark mode and keep their keyboard, focus and accessibility:
  `BUTTON` (Filled is the accent button, Plain accent text, Tinted an
  accent wash, Gray and Bordered the standard button), a check box
  drawn as Fluent's toggle switch (Win32 has no switch), the trackbar
  as Fluent's slider, `EDIT` in a frame the host draws (secure with a
  password character, multiline with return, Number with `ES_NUMBER`),
  a drop-down list `COMBOBOX` for `Wheel` and `Picker`, drawn as WinUI
  3's `ComboBox` (it paints its own box, its list's items owner-drawn on
  the flyout surface with the chosen item's accent pill, the list's
  window padded and rounded by DWM; a contrast theme's system colors;
  a Picker's label drawn while none is chosen and its Dynamic
  Annotation name), the date and time picker (inline, a month
  calendar, with a time picker under it for a date and time), two buttons for
  `Stepper`, a swatch opening the color dialog for `ColorPicker`. The
  segmented control, progress bar and ring are the host's own views,
  drawn as Fluent's. Each control sits in a view of the host's, which
  the core places: some controls send their notifications to the parent
  they were made in, so a control never changes parent. The month
  calendar has no dark visual style: dark, it's drawn classic in the dark
  palette's colors (its title and days' names in the accent's), the date
  picker's drop-down one likewise; the date and time picker's field,
  which has none either, paints itself light (its text antialiased in
  grays) and is turned dark, white to the dark field's background and
  black to its text.
- Fonts are Segoe UI Variable (Segoe UI before Windows 11), its optical
  size picked by the size (Small below 12.5, Display from 20); the text
  styles are the Windows 11 type ramp's nearest: LargeTitle is Title
  Large (40 semibold), Title is Title (28 semibold), Title2 Subtitle (20
  semibold), Title3 Body Large (18), Headline Body Strong (14
  semibold), Body and Callout Body (14), Subheadline 13, Footnote and
  Caption Caption (12), Caption2 11. Monospace is Cascadia Mono, serif
  Georgia; the app's fonts are added for the process. Symbols are SF
  Symbol names drawn as Segoe Fluent Icons glyphs, a curated set (one
  with no glyph shows nothing and is reported once).
- A `Stack` shows its top screen under a header bar of its own: a back
  button (Alt+Left too) while there's a screen to go back to, the
  screen's title, its bar buttons, and its search field
  (`ScreenOptions.Search`: a Fluent text box with the search glyph
  before the buttons, up to 280 points wide, as WinUI puts an
  AutoSuggestBox in a command bar; typing is the screen's TextChanged,
  Enter its Submit). `Tabs` are a bar on top, as WinUI's
  top `NavigationView`; a tab's or a pane item's badge is Fluent's
  `InfoBadge`, drawn, and the bar's or pane's UI Automation name is
  the chosen one's title and badge. A `Sheet` is a modal window over the window
  it's declared in (its `Window` prop: the main one, a `Window`, or the
  sheet shown over it; the owner disabled meanwhile; closing it or
  Escape dismisses it), half the window's height for a medium-only
  sheet. An `Alert` is a task dialog over the window it's declared in,
  likewise; an action sheet's buttons are command
  links. An alert with text fields is a dialog of the host's own, as
  WinUI's `ContentDialog`: its title and message,
  the host's Fluent text boxes, its buttons in a band below with the
  default one in the accent color; Tab, Enter and Escape as a dialog's,
  the owner disabled meanwhile. A `Snackbar` is a child window at the
  bottom of that window, drawn as Fluent's InfoBar, with BUTTON controls
  for its action and close, raised with a UI Automation notification. A
  menu is a
  popup menu: a button's drops below it, a view's `LongPress` menu is
  its context menu (a right-click, or a long press).
- A `Window` is a top-level window of its own (resizable, its own title
  and taskbar button; 800 by 600 points if the app gives no size),
  placed cascaded from the window in use. Its content, the node's
  child, is laid out at its client size, reported as its
  `Environment` when it opens and as it's resized; its least size is
  `WM_GETMINMAXINFO`'s. Closing it sends `Dismissed`, and the core
  destroys it (with the sheets over it); it shows once the main window
  does. Stacks in it have their header bar as anywhere, and Alt+Left
  pops the innermost stack of the window it's pressed in.
- Scale is per top-level window: the main one, each `Window` and each
  sheet has its DPI's (a `Window` or sheet opens at the DPI of the
  monitor it opens on). Each node knows its window's scale (`k`, 0 for
  the main window's), which it takes when it's inserted into a view of
  another window's, as list rows and cells, header and tab bars and
  scroll thumbs take their view's; points become pixels, fonts are made
  (cached by their size in pixels), text is measured, controls are
  sized and custom-drawn controls and scroll bars draw at it, and the
  window's `Environment.Scale` is it (the root's for the main window, the
  `Window`'s or `Sheet`'s, and their screens'). On `WM_DPICHANGED` a
  window takes the size Windows suggests and its views their new scale:
  fonts set again, frames placed again in pixels, lists tiled again,
  rich text formatted again; its environment is reported, and the core
  measures what's in it again (its measure cache is keyed by the scale
  of the element's nearest provider, so two windows at two scales never
  share a measurement) and snaps its frames to its pixels. The other
  windows keep theirs.
- The app's menus (`MenuBar`) are a menu bar on the main window only,
  as Windows apps' secondary windows (a tool window, an inspector) have
  none; their shortcuts work in every window of the app. The bar is
  made again (`CreateMenu`, `SetMenu`) each time the core sends the
  menus, none while it sends none: each title and item gets an access
  key (its first letter no other took: Alt+F), an item its shortcut
  after a tab ("Ctrl+Shift+K", "Ctrl+Enter", "F5"; Primary and Control
  are both Ctrl), a disabled one grayed, a checked one checked
  and a destructive one red (below). The shortcuts are an
  accelerator table (`CreateAcceleratorTable`), translated in the
  message loop against the window the key is for: a character is its
  virtual key on the keyboard's layout (`VkKeyScan`, with Shift if it
  takes it), the named keys theirs; disabled items have none. A chosen
  item, clicked or by its shortcut, comes as `WM_COMMAND` (ID 0x100 and
  up) and goes to the core as `Action` on the root. Popup menus follow
  the app's dark mode through uxtheme's app mode; the bar, which
  Windows draws light whatever the theme, the window draws dark while
  the app is (uxtheme's undocumented `WM_UAHDRAWMENU` and
  `WM_UAHDRAWMENUITEM`, as Notepad++ and MyGo do, with Windows 11's
  rounded backplates). A menu bar set before the window shows keeps
  its client size.
- A destructive item (a `MenuItem` with `Destructive`, an `Action` with
  `ActionDestructive` in a popup menu) is red, Fluent's critical text
  color, as macOS's (its title; its shortcut stays the menu's color).
  A Win32 menu can't color an item, so a menu with a destructive item
  is owner-drawn (`MFT_OWNERDRAW`, `WM_MEASUREITEM`, `WM_DRAWITEM`),
  all its items: one owner-drawn item takes the visual style off the
  menu's others, which the system then draws classic. They're drawn as
  the system draws a themed menu's, with the visual style's Menu parts
  (Windows 11's `ImmersiveStart::Menu`, `DarkMode_ImmersiveStart::Menu`;
  as the system's, light menus' separators are the plain `Menu`
  class's and dark menus' check marks and disabled gray
  `DarkMode::Menu`'s) at the owner window's DPI: the menu font, the
  height and margins (the check mark's), the check mark, the separator,
  the shortcut column (the submenu arrow's margins to its right),
  disabled gray; selected, the rounded backplate (`MPI_HOT`) under the
  mouse and a hairline rounded outline from the keyboard (the item's
  `ODS_NOFOCUSRECT` says which). The menu window would get the classic
  frame, so while such a menu is open a thread CBT hook subclasses the
  menu windows made (`#32768`): the frame is painted the menu's
  background, and DWM rounds the corners (`DWMWCP_ROUNDSMALL`) and
  draws the border in Windows 11's menu border color. Measured against
  the system's own menu at 192 DPI, light and dark, the menu is the
  same size and its items' pixels the same but the outline's corners.
  The items keep their text, so access keys, first letters and arrows
  work as in any menu; a key no item takes leaves it open
  (`WM_MENUCHAR`, `MNC_IGNORE`).
- `WindowOptions` set the main window's title, its client size
  (centered in the work area; 800 by 600 points if the app gives none)
  and its least size (320 points square if the app gives none).
- Windows' options (the main window's, a `Window`'s `Config`) are applied
  before the window shows: the abilities taken away are
  window styles (no `WS_THICKFRAME` and maximize box for a window that
  can't be resized, no `WS_MINIMIZEBOX` or `WS_MAXIMIZEBOX`; one that
  can't be closed has its close button and the system menu's Close
  grayed, and the user's `WM_CLOSE` and `SC_CLOSE` ignored), the most
  size `WM_GETMINMAXINFO`'s as the least is, always on top
  `HWND_TOPMOST`, a place the visible frame's top left
  (`DWMWA_EXTENDED_FRAME_BOUNDS`) in points at the window's scale. Left
  out of the taskbar, a window is owned by the host's hidden window:
  Windows gives owned windows no taskbar button, which holds as it shows
  again and as Explorer restarts (`ITaskbarList::DeleteTab` would have to
  be redone then); it stays in Alt+Tab. A child `Window` is owned by its
  parent's window (above it, minimized and hidden with it), a modal one
  disables its owner while it's open, as a sheet does, and enables it
  again before it's destroyed, so the owner is activated. A background
  fills behind the content wherever the host paints the window's
  background. Transparent isn't: every view is a child window, composed
  over the top-level window's opaque surface, so a transparent window
  shows its background color.
- A hidden title bar is MyGo's: `WM_NCCALCSIZE` gives the client area
  the whole window (Windows 11 keeps a pixel at the top, without which it
  opens no snap layouts; a maximized window fits its monitor's work
  area); the caption buttons are a layered child window drawn as Windows
  11's, in Segoe Fluent Icons (Segoe MDL2 Assets on Windows 10),
  answering `HTMINBUTTON`, `HTMAXBUTTON` and `HTCLOSE`, so snap layouts
  open over maximize (at the top left, close first, right to left); an
  invisible top edge answers `HTTOP` and its corners, so the window
  resizes from the top; and the top 32 points answer `HTCAPTION` where
  the views let the mouse through (a view that takes no input, a stack's
  header bar, answer `HTTRANSPARENT`): the strip drags, a double click
  maximizes, a secondary click opens the system menu. `SafeTop` is 32
  (0 in full screen). The main window has no menu bar under a hidden
  title bar (its shortcuts still work): Windows would draw it under a
  caption that isn't there.
- The actions are `ShowWindow`'s (`SW_SHOWNOACTIVATE` while
  snapshotting), `SetWindowPos`'s, and for `Focus` a restore from
  minimized, then `BringWindowToTop` and `SetForegroundWindow`, which
  Windows' foreground lock allows after the user's input to the app (a
  click on its tray icon, in its windows). Full screen takes the caption
  and frame off and covers the monitor, keeping the placement to put
  back. A size or place restores a maximized or minimized window first.
  The state is reported on `WM_SIZE`, `WM_MOVE`, `WM_WINDOWPOSCHANGED`
  and `WM_ACTIVATE`; a minimized window keeps its last place and size.
  The user's `WM_CLOSE` (the close button, Alt+F4, the taskbar's menu)
  on a window the core asks about sends `CloseRequest` instead of
  `Dismissed`, or of quitting for the main window; `WindowAction` Close
  takes the main window's closing path (its placement saved, destroyed,
  the app quits).
- Each window comes back where the user left it, as macOS's frame
  autosave has it: its normal rectangle (screen pixels, and the DPI it
  was at) and whether it was maximized, in `asuka-window.json` in the
  app's data directory (`ASUKA_DATA`, Local AppData\<name>; a file rather
  than the registry, so it goes with the app's other data, and `asuka
  dev` keeps it in its work directory). It's saved when a move or
  resize ends (`WM_EXITSIZEMOVE`), on maximize and restore and keyboard
  snaps (`WM_SIZE`, `WM_MOVE` outside a drag), and on close, not on
  every pixel of a drag; and restored before the window shows, over
  `WindowOptions`' size (their least size still holds): moved to its
  monitor first, so the window takes that DPI, then sized (scaled if
  that monitor's DPI is another now), and put whole on the nearest
  monitor's work area (`MonitorFromRect`) if it's off the monitors or
  mostly off one. The host keeps the normal rectangle itself, in screen
  coordinates (`GetWindowPlacement`'s are workspace coordinates). Each
  is kept by its key (the main window's `main`, a `Window`'s
  `StateKey`), a `Window`'s saved as it closes and restored over its
  options, within its least and most size. Tests (`go test`) and
  automation runs (`ASUKA_SNAPSHOT`, `ASUKA_SCRIPT`) keep none, and the
  data directory is made by what writes there.
- `List` is core-virtualized, as on macOS: rows the host places at
  their offsets in the list's window (one `DeferWindowPos`), binds
  through `ListBind` and recycles. Scroll bars are an indicator over the
  content, as WinUI's: a thin thumb (a window shaped by a region) shown
  while scrolling and under the mouse, which grows and drags; Win32's
  own scroll bars would take their width from the content the core laid
  out.
- `RichText` is a read-only rich edit (msftedit): spans are character
  formats, links the host's own (colored, hit-tested and clicked by the
  host, as the control draws its links in a color of its own), inline
  components transparent pictures of their size (`EM_INSERTIMAGE`) with
  their views placed on them; its background is sampled from what's
  behind it (a rich edit can't be transparent). Selection across texts
  in a `Selectable` isn't one selection yet: each text selects its own.
- Pinches and rotations: a touch screen's two fingers (`WM_GESTURE`'s
  zoom and rotate, rotate asked for with `SetGestureConfig` as a view
  listens), and a precision touchpad's pinch, which Windows sends as the
  wheel with Ctrl (so is a mouse's Ctrl and wheel, 10% a notch: began
  with the first, ended once the wheel rests for 250 ms).
- The host's modules (which the standard library's packages call):
  `secureStore` in the Credential
  Manager (the user's generic credentials, `<app>/<key>`, up to 2560
  bytes a value), `device` (info, the clipboard's text, keeping the
  display on), `browser`, `photos` and `files` (the open file dialog,
  the files copied to the caches), `web` (`Runtime.evaluate` through
  WebView2's DevTools protocol, awaiting a promise; `Network.getCookies`)
  and `js` (a blank page in a WebView2 that's never shown); `share`
  (Windows's share UI, `IDataTransferManagerInterop` and the
  `DataTransferManager` of the window in use: text, a link, files as
  `StorageFile`s through an `IIterable` of the host's; `share()` too),
  `network` (the Network List Manager: connectivity and its changes
  through a connection point, the connection's adapter's type, the cost
  as metered and constrained), `speech` (SAPI's `ISpVoice`, its events
  posted to the message window; the pitch SAPI's XML's), `audio` (an MCI
  `mpegvideo` device per player, a play's end `MM_MCINOTIFY`), `notify`
  (toasts, `ToastNotificationManager`; scheduled ones
  `ScheduledToastNotification`; the app registers its AppUserModelID, a
  Start menu shortcut with it and the user's
  `Classes\AppUserModelId`, the first time, and a COM activator, a CLSID
  of its AppUserModelID's whose `LocalServer32` is its executable,
  named by the shortcut's `ToastActivatorCLSID` and the key's
  `CustomActivator`: its class object, registered as the app starts,
  gets `INotificationActivationCallback.Activate` with the toast's or
  button's arguments, JSON carrying the notification, and the reply's
  text; started by COM for a click (`-ToastActivated`, taken out of
  the arguments before the app sees them) the app keeps its window
  hidden for a background action and quits once it's done with it;
  `showing`
  is `ToastNotificationHistory`'s toasts, read back from their XML; the
  badge an overlay icon on the taskbar button,
  `ITaskbarList3::SetOverlayIcon`, the count drawn, or
  `BadgeUpdateManager`'s for a packaged app) and `location` (WinRT's
  `Geolocator`, `RequestAccessAsync` first; place names from
  OpenStreetMap's Nominatim, a request at a time and a second apart,
  with a User-Agent naming Asuka, answers cached, as geocode-glib asks
  it). WinRT is called through its COM vtables in C: HSTRINGs and
  activation factories from combase.dll, the IIDs of parameterized
  interfaces computed from their signatures, delegates of the host's
  whose `Invoke` runs on the thread WinRT calls it on and posts to the
  main thread.
- More of what the standard library does on Windows, each part its own
  (their windows of a class of their own; the packages that are native
  packages have their Windows code in C, RFC 0008): `audioRecord` (waveIn, encoded as it
  records by Media Foundation's sink writer to AAC in MPEG-4, or ADTS
  when streaming; 44.1 kHz, Windows's AAC encoder's), `camera` (Media
  Foundation's capture engine: the preview sink draws into the view's
  window, filling it; photos through the photo sink, JPEG; videos
  through the record sink, H.264 and AAC; `take` a dialog of the host's
  with a live preview, Take Photo and Cancel, as on a Mac; with scan
  on, the preview sink's sample callback gives the frames in RGB32,
  which the view draws itself and reads codes in four times a second), `webAuth` (a window over the one in use,
  which waits as for a sheet, with a WebView2 in it: the app's profile,
  or a throwaway one for Ephemeral; it ends when a navigation's scheme
  is the callback's), `biometric` (Windows Hello:
  `UserConsentVerifier` through its interop, over the window; the
  sensor's kind from the Windows Biometric Framework), `imageEdit` (WIC
  decodes, upright from the EXIF orientation; the package does the
  actions and writes; a video's frame from Media Foundation's source
  reader),
  `vision` (Windows.Media.Ocr's lines and words, FaceAnalysis's faces,
  Asuka's own barcode reader's codes) and `scan.file` (Asuka's own
  reader: QR codes, Data Matrix, EAN, UPC, Code 128 and Code 39, found
  at any angle; Windows has no reader for apps), `photoLibrary` (the
  Pictures folder, no permission to ask; screenshots are its
  Screenshots folder's), `library` (the known folders), `clipboard`
  (its image as PNG, from `PNG` or a DIB; copying one; changes by its
  sequence number), `background` (`ES_SYSTEM_REQUIRED` while a task
  runs), `periodic` (timers while the app runs, the tasks kept in its
  data), `screen` (`WDA_EXCLUDEFROMCAPTURE` on every window, those that
  show later through a WinEvent hook; screenshots seen as files appear
  in the Screenshots folder), `tracking` (the advertising ID's setting),
  `contacts` and `calendar` (Windows gives the stores to packaged apps
  only: denied; adding opens a vCard or an iCalendar file in the user's
  app), and Apple's, Google's and packaged apps' modules (`appleAuth`,
  `ageRange`, `integrity`, `push`, `widget`, `review`), `splash`,
  `sensors` and `transcribe` answer what they would on a device without
  them. `asuka.map` is a view the host draws (Windows's MapControl is
  UWP's and needs a Bing Maps key): OpenStreetMap's raster tiles,
  two downloads at a time with a User-Agent naming Asuka, kept a week in
  the caches, the attribution on the map; dragged, zoomed with the
  wheel, a pinch, a double click and the keys; pins with a callout; the
  region reported once the user has moved it.
- Screen readers (Narrator, NVDA) read the host's views through UI
  Automation: each view of the host's own drawing answers `WM_GETOBJECT`
  with a provider of the host's (a COM object in C,
  `IRawElementProviderSimple`, made once per view, on top of its
  window's host provider) giving its name (Label; a text's text; a
  pressable view's texts), help text (Hint, else Tooltip), control type
  (Role; a pressable view a button; a header a heading), whether it's
  skipped (Decorative, with what's in it), the Invoke pattern for a view
  that presses, and a ProgressRing's value (the RangeValue pattern, read
  only, as WinUI's ring has it). A tab bar or a pane is named for its
  chosen item and its badge ("Inbox, 3 items"). `Announce` raises a UI
  Automation notification. A view's screen reader actions are its
  context menu (Shift+F10, the menu key, a right-click); an adjustable
  view takes the arrow keys, as a slider does.
- Shadows are drawn by the view a view's window is in, under its
  children, around the view's shape: a child window can't draw outside
  itself. They're concentric rounded rectangles from 3σ outside the
  shape to 3σ inside it, whose compounded alpha follows a
  Gaussian-blurred edge of σ half the Radius, as the other platforms'
  shadows. Transitions and matched views move the frame, as on macOS,
  stepped with the animation; their fades fade the view as opacity
  does. A view going out keeps its window, and those in it, until it's
  gone.
- Pull to refresh is F5, the Windows way: the refreshable Scroll or List
  the focus is in, or else the innermost one on screen; a spinner floats
  at the top of its content while it refreshes.
- A WebView2 controller is closed, and its window destroyed, on a later
  turn of the message loop, never inside one of its own callbacks (a
  DevTools call's answer, a page's event the app destroys the view in):
  WebView2 frees what the callback is running in (seen with the Runtime
  156's `CdpHelper`). DevTools answers come on a later turn too.
- `WebView` is WebView2 (the runtime found in EdgeUpdate's registry
  keys, as its loader finds it, so no loader DLL ships); the app's
  assets are served from `https://asuka.assets/`; pages follow the app's
  dark mode. `Video` is a WebView2 page of a `<video>`, with Edge's
  controls: Win32 has no video view with controls.
- Frames are set with `SetWindowPos`, without copying the old pixels
  (a view's pixels depend on what's behind it). `animate { }` steps the
  frames on a timer, and transforms with them.
- The host builds for amd64 and arm64: with Zig's MinGW target from any
  system (`internal/appbuild`'s `WindowsHost`, `internal/cbuild`'s
  `Program`, the core's objects cached by target), or clang or MSVC on
  Windows; C17, `-Wall -Wextra -Werror`. Windows's libraries are linked
  by their import libraries; what not every Windows or MinGW has
  (`TaskDialogIndirect` in common controls v6, `OpenThemeDataForDpi`,
  the dark mode's uxtheme ordinals) is looked up at run time, after the
  common controls v6's activation context is active. WebView2's client
  library is the runtime's of the process's architecture.
- Debug automation: `ASUKA_SNAPSHOT=file.png` writes the window (and the
  top sheet's, `file.sheet.png`, and each `Window`'s, `file.window.png`,
  `file.window2.png`…) with `PrintWindow`, and a menu open, `file.menu.png`,
  from the screen, once it has shown, without
  taking focus, then quits (`ASUKA_DUMP=1`: the view tree and the menu
  bar beside it); `ASUKA_SCRIPT` runs steps first (`tap:<text>`,
  `type:<text>`, `submit`, `back`, `escape`, `scroll:<y>`,
  `resize:<w>,<h>`, `hover:<text>`, `press:<text>` and `release` (a
  press held), `drop:<view>|file:…|text:…|link:…`, `drag:<view>|<target>`,
  `paste:<field>|…`, `pinch:<view>|<scale>`,
  `key:<shortcut>` such as `key:ctrl+shift+k`,
  `key:` also goes to a menu open (`key:down`, `key:d`), `menu:<item
  title>`, `open:<menu title>` (a menu bar's menu, shown below its title;
  `escape` closes it), `close` (the window in use, or the last
  `Window`), `dpi:<dpi>` (that window as if moved to a monitor at that
  DPI: `WM_DPICHANGED`, the title bar Windows draws keeping its own),
  `wait:<duration>`, `snap:<file>`), through the controls'
  own messages and the message loop's key handling (typing goes to the
  field last tapped when the app isn't the foreground one, as when a
  test starts it);
  `ASUKA_COLOR_SCHEME` is light or dark whatever the system's.

How the Linux host maps Asuka (`platform/linux`, C17):

- One executable: the host (platform/linux/src, with platform/desktop's
  shared C), the core and the app compiled to C (lang/cgen), linked
  statically; asuka dev's player is the same without an app, which loads
  the bytecode asuka dev sends (`asuka_dev_connect`). GTK 4, libadwaita
  and the GLib stack are opened with dlopen as it starts and their
  functions resolved with dlsym (`syms.def`: the subset the host calls,
  declared in `gtk.h` with GTK's own type names; optional libraries,
  WebKitGTK, GStreamer, Xlib, libsoup, libepoxy, Evolution Data Server,
  when something first needs them), so the host builds from any system
  with Zig and no GTK sysroot, and an app that lacks GTK says which
  library to install. The host makes an `AdwApplication` (its ID
  asuka.toml's `id`; unique, so a second launch activates the first, but
  in tests and automation runs) and runs GLib's main loop on the main
  thread. `asuka_desktop_post` and the core's posts go through one FIFO
  and an idle source (`g_idle_add`, thread-safe). Signals are connected
  with the node as their data, so a node's going disconnects them all.
  Its own symbols are `lx_`; it reads the core's batches with the core's
  own decoders (core/src/ops).
- Every node's view is a `AsukaView`, a `GtkWidget` subclass the host
  registers (`g_type_register_static_simple`, its class's vfuncs set from
  Go at GTK 4's stable offsets): `size_allocate` places its children at
  the frames the core gave them (whole sizes, the fractional origin in
  the allocation's transform, with `Style.Transform` about the center),
  `snapshot` draws the node's shadow, background or gradient, its own
  content (an image, a symbol, a shape, a canvas), its children (clipped
  to its rounded shape when it clips) and its border over them, as a
  layer does, and `measure` asks for nothing (the core lays out), but a
  scroll's content asks for its frame, which the viewport scrolls. A
  control (a `GtkLabel`, `GtkButton`, `GtkEntry`...) is the only child
  of its node's view, filling it less the content inset. A widget is
  measured by itself (`gtk_widget_measure`), a text by a Pango layout of
  the label's (as wide as its widest line, min-content its widest
  word). Opacity is the widget's, so it fades its children too.
- The environment is the window's content's size, its surface's scale
  (fractional), the desktop's text scaling (GTK's Xft DPI over 96, as
  FontScale), libadwaita's dark style, animations off as Reduce Motion,
  GNOME's screen reader setting and clock format, the locale (LC_ALL,
  LC_TIME, LANG), the languages (LANGUAGE) and the time zone
  (/etc/localtime). `Environment.Platform` is `PlatformLinux`.
- Colors are libadwaita's named colors (`window_bg_color`,
  `card_bg_color`, `accent_color`...), read from the theme for the
  current style; asuka.toml's accent is the theme's accent, through a
  style sheet of the host's. The text styles are libadwaita's type
  classes relative to the interface font (gtk-font-name): LargeTitle is
  `.title-1`, Title `.title-2`, Title2 `.title-3`, Title3 `.title-4`,
  Headline `.heading`, Footnote and Caption `.caption`. Text is styled
  with Pango attributes (no style sheet per view). The app's fonts are
  fontconfig's application fonts.
- Controls are GTK's and libadwaita's: Filled is `.suggested-action`,
  Plain `.flat`, Tinted a wash of the accent; `GtkSwitch` and
  `GtkCheckButton`, `GtkScale`, `GtkEntry` and `GtkPasswordEntry` (a
  `GtkTextView` for Multiline), a linked box of toggle buttons for
  `Segmented`, `GtkDropDown` for `Wheel` and `Picker`, linked minus and plus buttons
  for `Stepper`, `GtkColorDialogButton`, a date button opening
  `GtkCalendar` and hour and minute spin buttons, `GtkProgressBar`,
  libadwaita's spinner. Images are decoded off the main thread by GDK's
  loaders (animated GIF, APNG and WebP by platform/desktop's anim.c, their
  frames' pictures GDK's) into textures the view draws at its
  mode, tinted through a mask. Symbols are the icon theme's symbolic
  icons where freedesktop or Adwaita has a match for the SF Symbol name,
  else the Material Symbol (as Android and the web draw them), written
  once as a symbolic SVG into an icon directory the theme searches, so
  every place that takes an icon name (buttons, menus, tabs) takes them.
- Text fields: a Multiline field's `GtkTextView` is styled as libadwaita
  styles an entry (filled, rounded, the accent's focus ring), its
  placeholder a dim label shown while it's empty, its font a tag under
  its text; it's measured by a Pango layout of its text as the view lays
  it out. A Rich field's styles are a text tag per style (Ctrl+B, I and
  U toggle formats); typed text takes the style before it. Selection
  offsets cross as UTF-16 (the core's) and characters (GTK's).
  PasteTypes: a paste of files, an image (as PNG) or data of a type the
  field takes (GDK's clipboard formats) is read asynchronously into the
  app's caches and reported with Pasted instead of going in. Autocapitalize,
  Autocorrect and ContentType are the input method's purpose and hints;
  ReturnKey labels a touch keyboard's key, so does nothing here.
- `RichText` is a `GtkLabel` styled with Pango attributes: the label's
  layout draws what a layout of the same text and attributes measured.
  Links are the host's (colored and underlined as a label's, hit-tested
  in the layout, a hand over them, a click claimed); inline components
  are shape attributes of their size on the baseline, their views placed
  where the layout puts them. A `Selectable` selects across its texts:
  GTK keeps one label's selection at a time (a label that selects takes
  the primary selection from the one that had it), so the selection is
  the host's. A drag on the `Selectable`'s view (claimed in the capture
  phase) maps the pointer to a text and a character through each
  label's Pango layout, each text between the ends in reading order has
  its part, drawn under its label (GDK's clip region of the range) in
  libadwaita's selection color; a double click selects words, a triple
  click paragraphs, Shift+click extends, Ctrl+A selects all, and Ctrl+C
  and the context menu's Copy (GTK's words) copy the parts, a paragraph
  each. A press on a link, a control or a pressable view is theirs.
- Each window has one header bar (an `AdwHeaderBar` in an
  `AdwToolbarView`), as GNOME's apps do: the innermost stack on screen's
  back button, its top screen's title, bar buttons and search (a search
  button, and GTK's search bar under the header, which typing in the
  window opens), or the tabs' view switcher; the app's menus
  (`MenuBar`) are its main menu, the last button, their shortcuts the
  application's accelerators in every window. A `Stack` is an
  `AdwNavigationView` (its pages slide; a swipe, the back button,
  Alt+Left and Escape pop, with `Popped`), `Tabs` an `AdwViewStack` whose
  `AdwViewSwitcher` is in the header bar on a wide window and an
  `AdwViewSwitcherBar` at the bottom of a narrow one (under 550 points),
  a tab's badge its page's `badge-number` (a count) or `needs-attention`,
  a `Sheet` an `AdwDialog` (a floating dialog, a bottom sheet in a
  narrow window), an `Alert` an `AdwAlertDialog` (the first default
  button suggested, a destructive one red, Escape the cancel button;
  its fields a boxed list of `AdwEntryRow`s, its extra child),
  a `Snackbar` an `AdwToast` in the `AdwToastOverlay` around the
  window's content (its button-label and button-clicked, its timeout),
  each presented on the window it's declared in (its `Window` prop), a
  `Window` an `AdwWindow` of its own. Screens, sheets and windows are
  sized by GTK: each reports its `Environment` as it's allocated.
- Menus are `GMenu` models in `GtkPopoverMenu`s, their items actions of
  a group on the view: a button's drops from it, a view's LongPress menu
  is its context menu at the pointer (a secondary click, a long press).
- Lifecycle, as on a Mac: Background while no window of the app's
  shows (hidden, or its GdkToplevel minimized or suspended), and as it
  quits (its last window closing, GtkApplication's `query-end` as the
  session ends, `shutdown`, or SIGTERM, SIGINT and SIGHUP, which quit it
  as closing it does), so `asuka/sdk/store` writes what's pending; GLib's
  memory monitor's warnings are MemoryWarning. Crash reports: the core's
  record (`asuka_crash_start`), and a fatal signal in native code caught
  on an alternate stack (crash.c), its frames written to the record's
  inbox with write(2) before the signal's default action runs.
  Windows are placed by the desktop: GTK 4 has no way to place one.
- Input is GTK's event controllers, added as the core listens: a click
  (Press, Pressed; Enter and Space press a focused pressable view, which
  Tab reaches), a long press or a secondary click (LongPress), a drag
  (Pan; one that ends fast a Swipe, as a touchscreen's flick is), zoom
  and rotate gestures (touchscreens, touchpads; Ctrl and the wheel
  pinch too), motion (Hover).
- Animation: GTK doesn't animate a widget's allocation, so between
  AnimateBegin and AnimateEnd the frames, opacity and transforms the
  core sets are stepped on the frame clock with the core's curve (CSS's
  cubic béziers, a damped spring); a view's first frame doesn't animate;
  enter and exit transitions start from, and go out to, the
  transition's state (a view going out stays until it has, with the
  views in it). `Style.Match` notes where the keyed views are as an
  animated batch starts destroying; the old view's widget waits on top
  of its top view (a screen's, the root's), then moves to the new one's
  place, fading, as the new one grows from its old place.
- Drawing is GSK's, in the view's snapshot: a `Shape`'s outline is a
  `GskPath` (from its SVG form), filled and stroked (`GskStroke`), its
  trim a `GskPathMeasure`'s segment, stepped on the frame clock inside
  an animation; a `Canvas`'s commands become paths, strokes,
  transforms and Pango layouts as they're set, replayed each frame with
  raw calls (Clip a fill clip, Alpha multiplying the colors). A `Shader`
  is its program as GLSL (package glsl, a std140 uniform block), drawn
  as the view snapshots, in a GL context of the display's (shared with
  GTK's renderer), into a texture that becomes a `GdkGLTexture` with a
  fence, no copy; the textures GTK lets go of are drawn into again, as
  GtkGLArea's are. Animated, the view's frame clock queues a draw each
  frame. Without GL it runs on the CPU (package interp), off the main
  thread, into a `GdkMemoryTexture`. A
  `PageControl` is drawn as libadwaita's carousel dots, measured, and
  takes clicks and the arrow keys. A paging `Scroll` has no scroll bar
  and settles each swipe (a wheel's notch, a touchpad's or a
  touchscreen's swipe, the arrow keys) on a page, with `Paged`. Pull to
  refresh is F5 or Ctrl+R (the focus's refreshable Scroll or List, else
  the innermost on screen), a spinner on a card over its top while it
  refreshes.
- Drag and drop is GTK's: a `GtkDragSource` (its files a `GdkFileList`,
  its texts a string, its links `text/uri-list`, and a private type of
  the app's) on a view with `DragItems`, a `GtkDropTargetAsync` on one
  with `DropAccept` (the app's own drag known by `gdk_drop_get_drag`;
  another app's text and URIs read as it comes over: files by their
  names' types, other URIs links). GTK has no accessible actions of an
  app's (to 4.22), but its AT-SPI bridge gives a widget with action
  groups of its own the Action interface, listing their actions by
  name: a view's `A11yActions` are its group "action", named after their
  titles as GAction names may be ("action.Mark-as-read"), which Orca
  lists and does; the view is a group (so its label is read), has a
  popup (has-popup) and the key Shift+F10, and the actions are its
  context menu (a secondary click, the Menu key, Shift+F10). An
  adjustable view is a slider to screen readers and takes the arrow
  keys.
- Each window comes back at the size the user left it, and maximized if
  it was (a Wayland app can't place its window), by its key in
  `asuka-window.json` in the app's data directory: `ASUKA_DATA`, which the
  host sets to the XDG data directory's folder for the app (Flatpak's
  own). It's saved as the window closes and as the app shuts down.
- Windows' options are GTK's, set before the window shows
  (`winctl.c`): its content's size (the default size, the header bar's
  height on top) and least (the content's size request), not resizable,
  not deletable, the header bar's decoration layout without its minimize
  or maximize button (the window manager's shortcuts still do them),
  maximized or full screen, hidden until a `Show`, a background color or
  none (the window's own CSS provider, above the app's; transparent with
  a compositor). A hidden title bar extends the content to the top edge
  under a flat, title-less header bar whose window buttons stay;
  `SafeTop` is its height. A child is transient for its parent and
  destroyed with it, a modal one modal too. GTK 4 has no most size, no
  way to place a window or center it (Wayland leaves it to the
  compositor, and GTK 4 dropped moving one on X11 too), and none to keep
  one on top or out of the taskbar: under X11 the host does those itself
  through Xlib on the surface's window (`desk.c`: `XMoveWindow`,
  EWMH's `_NET_WM_STATE_ABOVE`, GDK's skip-taskbar hint); under Wayland
  they do nothing. The position reported is 0, 0. The actions are GTK's
  (`gtk_window_present`, `_minimize`, `_maximize`, `_fullscreen`, a new
  default size); the state follows `notify::visible`, `is-active`,
  `maximized`, `fullscreened`, the surface's state (minimized: X11 only,
  as Wayland never tells an app) and the content's allocation. A window
  the core asks about keeps itself open on `close-request` and sends
  `CloseRequest`; `WindowAction` Close closes the main window on the next
  idle turn (the core answers while GTK is still emitting the request).
- Screen readers (Orca) read GTK's accessibility tree: a view's role (a
  pressable view a button, a header a heading), label and description.
- Debug automation, as on macOS and Windows: `ASUKA_SCRIPT` runs steps,
  then `ASUKA_SNAPSHOT=file.png` renders each window to a PNG
  (`ASUKA_DUMP=1`: the view tree beside it) and quits.
- The update module (Updates): `info` from where the executable started
  (an install prefix: `bin/<exe>` with `share/<exe>/`; Flatpak, Snap,
  AppImage and the system's prefixes managed), `download` and `next`
  through libsoup (a long poll answered as the progress moves by 1%, 64
  KB while the size is unknown), `cancel`, and `install`, which swaps the
  prefix's files and links for the staged tree's (`.asuka-files` naming
  them), puts back what it moved if a rename fails, and runs the
  desktop's cache tools. `relaunch()` starts the executable at the path
  it started from with `ASUKA_RELAUNCH_WAIT`, which the new process waits
  on before it takes the single instance; at launch the host removes the
  `.asuka-update-*` and `.*.old-*` an update left.
- Tests (`go test ./platform/linux`) build apps (testdata/*.asuka) into
  the executable an app ships and drive real GTK windows; they need a
  display and a session bus (`xvfb-run -a dbus-run-session`), and skip
  without them. test/fakes.c gives the session bus a notification server
  and a StatusNotifierWatcher, so notifications and the tray are driven
  too.

Desktop needs what mobile doesn't: several windows, the app's menu bar
and keyboard shortcuts, hover and right-click, resizable layouts. To
keep that additive, the core already treats the mobile screen as one
root with an environment (size, insets) that can change at any time,
and never assumes a single root in its own data structures.

**Windows, the menu bar and shortcuts** (core/src/ui/window.c,
menubar.c, keys.c; the Asuka API in docs/navigation.md):

- The main window holds the root. The app block's first `Window` is
  the main window (the lowering calls `ui.main_window` on it before its
  keys): its keys are the main window's options, sent to the root as
  the `WindowOptions` command as it mounts, its title's later changes
  as `WindowTitle`; its children are the root's content, built at once.
  Its `open` (true unless set) shows and hides it with `WindowAction`
  Show and Hide; the core follows the root's `WindowState` while `open`
  is bound, so the user closing it in an app that keeps running (the
  host hides it) writes false. With no options, a desktop host opens it
  at 800 by 600 points, a desktop window's size (Electron's default),
  not a phone's, centered. Where the user left it, remembered by the
  host, wins over its size and place.
- Any other `Window` is a window of its own while `open` is true, built
  as a `Sheet` is: its children are a builder (`ui.lazy`), built under
  an owner of their own as it opens and disposed as it closes; its view
  is a `Window` node, a host-sized layout root whose `Environment` the
  host reports (it follows the user resizing it; its Scale is the
  window's, which on desktop can differ from the main window's: the
  core measures and snaps the frames in it at that scale). `Present`
  opens it (shown, unless its config says hidden), `Destroy` closes it,
  and the user closing it sends `Dismissed`, which writes `open` false
  and raises `close` (or `CloseRequest`, while something asks). A
  `Window` declared in its content closes with it. Mobile hosts can't
  show one; the core makes none there only because nothing opens one.
- A window's options, the main window's and a `Window`'s alike, are its
  keys (`width`, `height`, `minWidth`…, `x`, `y`, `center`, `resizable`,
  `minimizable`, `maximizable`, `closable`, `alwaysOnTop`, `hidden`,
  `skipTaskbar`, `transparent`, `maximized`, `fullScreen`, `titleBar`,
  `background`, `child`, `modal`, `panel`, `flyout`, `fitContent`,
  `clickThrough`, `menuBar`, `stateKey`), encoded as one
  `ops.WindowConfig` (the `WindowOptions` command's args, the `Window`
  kind's `Config` prop) and read by one decoder in each desktop host:
  its content's size, least and most size, its place (its frame's top
  left in points from the main display's top left, or centered),
  abilities taken away, always on top, a hidden title bar
  (`TitleBarHidden`: the content under it, the window buttons over the
  content, the title bar's height reported as the window's
  environment's `SafeTop`, so `SafeArea` keeps clear of a Mac's traffic
  lights as of a phone's notch), a background or none (transparent),
  hidden, maximized or full screen as it opens, left out of the
  taskbar, a state key, and a `Window`'s parent (the node of the window
  it's declared in: a child stays above it, a modal one blocks it).
  They're read as it opens; `title`, `edited`, `file` and `flyout` are
  sent again as their bindings change, and `alwaysOnTop` and
  `clickThrough` as `WindowAction`s. The rest that changes later are
  the handle's actions, not keys, as a window's place and size are the
  user's as much as the app's.
- A window's handle is a `Ref`: `Window(…) as w` (a `Ref` of any other
  view names the window that view is in; none, the window the calling
  code is in, a context each window provides, else the main window).
  Its actions are one `WindowAction` command (u8 action, two floats for
  a size or a place) to the `Window` node or, for the main window, the
  root: `w.show()`, `hide()`, `focus()` (`ui.focus` of a `Window`),
  `minimize()`, `maximize()`, `restore()`, `setFullScreen(on)`,
  `center()`, `setSize(w, h)`, `setPosition(x, y)`, `setAlwaysOnTop`,
  `setClickThrough`, and `close()`, which is the user's closing (it
  asks first). Show and Focus on a closed `Window` open it, through its
  `open`. Its state (`w.visible`, `focused`, `minimized`, `maximized`,
  `fullScreen`, `alwaysOnTop`, `clickThrough`, `position`, `size`,
  `windowState`) is one signal per window, set from the host's
  `WindowState` event (the frame's top left, the content's size, seven
  bits), which a host sends only while the core listens: the core
  listens from the first read of a handle's state on; the rest cost
  nothing. The host reports at once as the core starts listening, then
  on every change, the user's or the app's; the core drops a report
  equal to the last, so readers don't re-run. Mobile hosts have no
  windows: the handle's actions go to the root, which they ignore.
- Closing asks: what asks are a window's `WindowCloseGuard`s (while
  their `when` holds, they run their `closeRequest` and the window
  stays) and the `onCloseRequest(fn() -> bool)` functions registered in
  it, each for as long as its owner lives; then a `Window`'s own `on
  closeRequest` (the main window's too) takes the closing over. While
  any of these exists, the core listens for `CloseRequest` on the
  window's node (the root for the main window): a host whose window the
  core listens on reports the user's closing with it and keeps the
  window open. The core asks each, and closes the window if all agree
  and no handler took it: a `Window` as the user's closing does (`open`
  false, `close`), the main window with `WindowAction` Close, which the
  host does as its own closing would (the app quits, or hides it while
  it keeps running). `closeWindow()` closes without asking, what a
  guard's question calls once the user has chosen. Quitting the app as
  a whole (⌘Q, the session ending) doesn't ask the windows: it asks the
  app (below).
- Where windows were is the host's, by the key the app gives
  (`stateKey`; the main window's is `main` when it gives none, a
  `Window`'s isn't kept without one): a Mac's frame autosave, the
  Windows host's rectangle in pixels with its DPI and whether it was
  maximized, the Linux host's size and whether it was maximized, in one
  `asuka-window.json` per app, and on the web a panel's place in
  `localStorage`.
- A window's keys: `WindowKeys { keys }` gives the window it's in
  (`Keys` on its `Window` node, or the root's, merged with
  `useKeys`') keys that no view takes, the last registration's winning,
  for as long as its owner lives.

- `MenuBar { Menu(title) { MenuItem(title) { shortcut, role, disabled,
  checked; on press } … } }` gives the app its menus, with keyboard
  shortcuts (a `Shortcut`'s key and its `primary`, `shift`, `alt`,
  `control`; `primary` is ⌘ on macOS and Ctrl elsewhere). `MenuItem`s
  in an item make a submenu, `MenuSeparator()` a line. None makes a
  view: a `MenuBar` builds its children (`ui.lazy`) under an owner of
  its own, outside the tree, where a `Menu` is a menu bar's menu (a
  context the `MenuBar` provides: elsewhere `Menu` is the context menu
  element), and marks what it holds as its, so a key set on an item, or
  an item a `for` or an `if` adds or removes, syncs it at the next
  flush. The last `MenuBar` shown is the app's: the core encodes its
  menus into one `MenuBar` command to the root and sends it only when
  the encoding changed; the host makes its menu bar again, and a chosen
  item (a click or its shortcut) comes back as `Action` on the root
  with the item's index among all the menus' items. Disposed, the one
  before shows; with none left, an empty bar is sent. Shortcuts are
  menu items' because that's how both platforms route them (key
  equivalents, accelerators), and how users find them. Mobile hosts
  ignore it.
- Items are numbered depth first (a submenu's items after the item
  that opens it; separators and submenus count), and `Action` carries
  that number. A `MenuItem`'s `role`: `.destructive` sets the item's
  destructive flag; `.quit` (`MenuRoleQuit`) quits the app as the
  system quits it ("Quit Notes", ⌘Q on a Mac), its title the host's
  unless the item has one, and is never reported; `.close`
  (`MenuRoleClose`) closes the window in use as its close button does
  (⌘W, Ctrl+W); `.openRecent` (`MenuRoleOpenRecent`) is a Mac's Open
  Recent submenu, the host's, and elsewhere the item's own.

**Menus, one encoding** (`ops/menu.go`, core/src/ops/payload.c's
`ak_append_menu_bar`): the menu bar, a tray icon's menu and the Dock
menu are the same bytes: a menu is u32 count, then per item u8 flags
(separator, disabled, checked, destructive, submenu), its title, its
shortcut's key and modifiers, u8 role, and with the submenu flag the
submenu, a menu. The core encodes the menu bar (menubar.c);
`asuka/tray` encodes its menus in Asuka (an `Action`'s `.section` its
items between separators, under a disabled item of its title when it
has one) and finds a chosen item by its number the same way; the
desktop hosts decode with `ops.DecodeMenu` and `ops.DecodeMenuBar`
(`ak_decode_menu`, `ak_decode_menu_bar` in C), which number the
items; the web host reads the menu bar's shortcuts with the same
layout.

**The tray, the Dock menu, background apps, quitting** (std/tray,
core/src/ui/lifecycle.c):

- `tray.Tray(id) { image, symbol, template, title, tooltip, menu:
  [Action]; on click }` shows an icon in the tray while it's shown:
  the menu bar on macOS, the notification area on Windows, the panel's
  status icons on Linux. Its keys are bindings; disposed, the icon
  goes. `tray.DockMenu { items }` adds items to the app icon's menu,
  the last shown showing, as `MenuBar`. Neither is a node: they're
  plain Asuka over the hosts' `tray` module (`asuka/platform`'s `call`
  and `send`): `set` sends an icon whole (u32 its number, 1 and up, u8
  flags: 1 template, 2 clickable, strings image, symbol, title, tooltip
  and id, then its menu), again only when its encoding changed;
  `remove` (u32 its number) takes it away; `dock` (a menu) sets the
  Dock menu; and `next`, asked again each time it answers, reports a
  click or a chosen item (u32 an icon's number, 0 for the Dock menu, u8
  1 a click or 2 an item, u32 the item's number). One `next` is
  outstanding while any icon or Dock menu shows (a `live` stream the
  icons read); the last one going cancels it, which sends `stop`, and
  the host answers the waiting `next` with an error. A host without the
  module (phones, the web) answers with an error, and the package stops
  asking: there, `Tray` and `DockMenu` show nothing and
  `tray.supported()` is false. `tray.anchor(id)` is a flyout's anchor:
  `Anchor(tray: n)`, the icon's number, followed.
- An icon is an `image` (an asset or a file, drawn 16 points tall) or a
  `symbol` (an SF Symbol name, drawn as each host draws `Symbol`), and
  a template when it's a symbol or `template` says so: black on
  transparent, which the platform colors as its own icons. Without `on
  click` (an event's name read as a value is whether the caller handles
  it) a click opens the menu; with it, a click is `click` and the menu
  opens on a secondary click.
- macOS: an `NSStatusItem` (variable length; `ID` its autosave name,
  so it comes back where the user ⌘-dragged it), its button the template
  image and the title in the menu bar's font with monospaced digits;
  without `OnClick` its menu is the item's own, with it the button's
  action opens the menu on a right or Control click (the menu set on the
  item for one `performClick:`). Its menu and the Dock's enable items as
  the app says (no validation) and show no shortcuts. The Dock menu is
  the application delegate's `applicationDockMenu:`, made when the Dock
  asks.
- Windows: `Shell_NotifyIconW` (`NOTIFYICON_VERSION_4`), the icons a
  hidden top-level window's (a message-only window doesn't get
  `TaskbarCreated`, on which every icon is added again). An Image is
  fitted to `SM_CXSMICON` at the system DPI; a Symbol is its Segoe
  Fluent Icons glyph, its gray antialiasing the alpha; a Symbol or a
  Template is white on a dark taskbar and black on a light one
  (`SystemUsesLightTheme`, drawn again on `ImmersiveColorSet`). The
  tooltip is `szTip`; Windows shows no title. `NIN_SELECT` and
  `NIN_KEYSELECT` are `OnClick`, else the menu; `WM_CONTEXTMENU` the
  menu, made by the menu bar's code (submenus, checks, destructive items
  owner-drawn), opened at the event's anchor after the tray window comes
  to the foreground, `WM_NULL` posted after it. `ID` is unused: an
  `NIF_GUID` is tied to the executable's path. The Dock menu is the
  jump list's Tasks (`ICustomDestinationList`): each top-level item a
  shell link to the executable with `--asuka-dock-item=<number>`, titled
  by `System.Title`; separators `IsDestListSeparator` links; submenus,
  disabled items and roles left out (a jump list can't show them); none
  deletes the list; tests and automation and dev runs don't touch the
  user's. The argument is taken out of `os.Args`; a running app (one
  with asuka.toml's `id` runs once) is handed `asuka-dock-item:<n>`, and
  reports the item without coming to the front. Submenus in the menu bar
  are `MF_POPUP`s, with access keys per level; an owner-drawn item that
  opens one draws the visual style's arrow (`MENU_POPUPSUBMENU`) and
  clips it out (`ExcludeClipRect`) so the system's classic one doesn't
  draw over it. Quit destroys the main window once the core has
  returned (Lifecycle Background first), which ends the loop and removes
  the icons. Keeping running, `WM_CLOSE` hides the main window.
- Linux: GTK 4 has no tray, and the GTK 3 appindicator libraries can't
  load in a GTK 4 process, so the host speaks their protocol itself
  through GDBus (`platform/linux/src/dbus.c`: objects exported with one
  vtable of callbacks, made once, routed by user data). Each icon is a
  StatusNotifierItem on a session bus connection of its own
  (`/StatusNotifierItem`), its menu a `com.canonical.dbusmenu` object
  (`/MenuBar`), registered with `org.kde.StatusNotifierWatcher` and again
  when that name gets a new owner; removing it closes its connection,
  whose name going every watcher follows. A Symbol is `IconName`, with
  `IconThemePath` the host's directory of Material symbols; an Image is
  `IconPixmap` (ARGB32, big-endian, at its size and 48 down to 16), a
  Template white while libadwaita's style is dark; the title is
  `XAyatanaLabel` (GNOME's AppIndicator extension shows it). Without
  `OnClick`, `ItemIsMenu` has the panel open the menu on a click; with
  it, `Activate` is the click. The dbusmenu's items have labels (`_`
  doubled), enabled, a checkmark toggle and `children-display` for a
  submenu; a destructive item is set apart by separators, as the
  host's GMenus set it apart; each change bumps the layout's revision
  (LayoutUpdated); a "clicked" Event chooses the item. KDE Plasma, Xfce,
  Cinnamon, MATE, Budgie and GNOME with the AppIndicator extension
  (Ubuntu's default) show it. The Dock menu is the launcher's quicklist:
  a dbusmenu at `/org/asuka/Quicklist`, named by
  `com.canonical.Unity.LauncherEntry.Update` for
  `application://<id>.desktop` (Ubuntu's Dock, Dash to Dock, Plank,
  KDE's task manager). Keeping running holds the `GApplication`; closing
  the main window hides it. The menu bar's submenus are GMenu submenus;
  Quit takes Ctrl+Q and quits as SIGTERM does.
- asuka.toml's `keepRunning = true` keeps a desktop app running when
  its last window closes: closing the main window hides it, its root
  mounted, until the app shows it again (on macOS also the Dock icon,
  clicked with no window showing:
  `applicationShouldHandleReopen:hasVisibleWindows:`; on Linux, the
  app launched again, `activate`). `background = true` (which on macOS
  also puts `LSUIElement` in the Info.plist so the Dock icon never
  shows) also leaves the main window hidden at launch, and on macOS
  makes the app an accessory (no Dock icon, not in ⌘Tab; a `Window` it
  opens activates it). Both reach the host in the settings beside its
  executable (`asuka-app.json`'s `keepRunning` and `background`), read
  before the app mounts. Windows and Linux have no app without a
  taskbar button but one without a window showing: that's theirs.
  Automation runs (`ASUKA_SNAPSHOT`) still show the main window; on
  macOS they write each tray icon's button beside it (`file.tray.png`)
  and, with `ASUKA_DUMP`, the icons' and the Dock's menus in the dump.
- Quitting: `quit()` sends the `Quit` command to the root (u8 flags, 1
  relaunch: `relaunch()`), which a desktop host does as the user's
  Quit (⌘Q, its menu's `.quit` item): it asks the app first, then
  closes the windows and ends the process (Lifecycle Background first,
  as always); with the relaunch flag, once the process has ended it
  starts the app again with the same arguments (a Mac app's bundle
  through Launch Services), unless the quit was canceled or the session
  is ending. Phones and the web ignore it.
- Asking: however a quit starts (the user's Quit, the Dock's, closing
  the main window where that quits, `Quit`, SIGINT or SIGTERM, the
  session ending), the host calls `asuka_quit_request(ending)` once,
  synchronously on the main thread, before it goes on: the core runs
  the app's `on quit(e)` handlers (the app block's and `onQuit`'s,
  each for as long as its owner lives) with a `QuitEvent`
  (`sessionEnding`, `cancel()`, `canceled()`), flushes what they
  changed (an alert to show), and returns 1 if one canceled, which
  never happens while ending. A canceled quit leaves the app running
  (macOS: `terminateCancel`); a later `quit()` asks again. A host asks
  once per quit: a quit already allowed only finishes.
- Another launch: on Windows and Linux an app with an `id` runs once (a
  mutex of the user's session and `WM_COPYDATA`; a unique
  `GApplication` over D-Bus), and the running one gets the second
  launch's arguments and working directory as the `SecondInstance`
  event to the root (u32 count, the arguments, the directory, each a
  u32-length string), after bringing itself to the front and reporting
  the files and URLs among them as it does a launch's. The core runs
  the app block's `on secondInstance(args, dir)` and
  `onSecondInstance`'s. A Mac runs an app once through Launch Services
  and passes files, not arguments: it never sends it.

**Flyouts, panels, windows sized to their content, click-through**
(`ui`, `tray`, `apps`; `internal/flyout`):

- Kinds of `Window`, not kinds of their own: `ops.WindowConfig`'s flags
  `WindowFlyout`, `WindowPanel`, `WindowFitContent` and
  `WindowClickThrough` (bits 14 to 17), set before the window opens
  (a `Window`'s keys `flyout`, `panel`, `fitContent`, `clickThrough`;
  the main window takes all but `flyout`). A panel also
  sets always on top, skip the taskbar, no minimize or maximize; a
  flyout skip the taskbar and its `Parent`, the window it's declared in.
- A flyout's anchor is the `Window` kind's `Flyout` prop (`ops.Flyout`:
  u32 tray icon, f32 x, y, width, height of a rectangle of its parent's
  content, u8 `Placement`, f32 gap), sent before `Present` and again as
  its binding's encoding changes (placed again). A tray icon is
  `asuka/tray`'s number for it (the "set" call's), which each host's
  tray module keys its icons by: `tray.anchor(id)` is that number.
  `internal/flyout.Place` is one placement for the desktop hosts: on
  the side the placement names, flipped to the other side where that
  has no room and the other has, lined up along it (flipped to the other
  end), slid into the work area, shrunk to it, as GTK places menus.
- A flyout and a panel take the keyboard as they show, and close as the
  user's closing does (CloseRequest, else Dismissed; the main window's
  close, which hides it in an app that keeps running) on an Escape the
  content leaves and as they lose the keyboard, unless a sheet, an
  alert or a modal `Window` of theirs shows: a modal `Window` whose
  parent is a flyout or a panel is a window above it, centered on it,
  not a sheet hanging from it, and gives it the keyboard back as it
  closes.
- Fit: a window sized to its content is a layout root the core lays out
  at its content's size, not the host's (`layoutFitWindow`: width auto
  within the least and most width, a second pass at the most width if
  it reaches it, so text wraps there; height auto within the least and
  most height), and sends `WindowAction Fit` (its size, as `Size`'s
  args) in the frames' batch whenever that size changes, once at the
  least as it opens. The host sizes the window in the same crossing: a
  flyout placed again, other windows keeping their top left; a window
  that opens shown with it shows on its first Fit, so never at another
  size first. The environment the host then reports doesn't lay the
  content out again (the content isn't laid out at it). The main window
  sized to its content is the root laid out so, its Fit sent to the
  root (core/src/ui/root.c). The tab on the web isn't fitted.
- Click-through is `WindowAction` ClickThrough and NotClickThrough, and
  the `WindowState` bit 64 (`w.clickThrough`).
- macOS: flyouts and panels are `AsukaPanel`s, `NSPanel`s made
  nonactivating and borderless from the start (the window server learns
  that a window never activates its app only from the style it's made
  with, and forgets it if the style is set again: `configure` leaves
  their style alone; the main window, made before its options, is made
  again as one as `WindowOptions` says so), clear under an
  `NSVisualEffectView` (a menu's material for a flyout, a popover's for
  a panel) unless the app gives a background, the content's layer
  rounded, the window's shadow. They can be key, never main:
  `makeKeyAndOrderFront:` leaves the app the user is in frontmost (NSApp
  reads active meanwhile; the workspace's frontmost application
  doesn't change). A panel joins all Spaces as a full-screen auxiliary,
  out of the window cycle, at the floating level; a tray flyout is at
  the level of menus, under the copy of the status item the user
  clicked (the click's place less its place in the item's window, as
  the window isn't always where the copy shows: one per display's menu
  bar), or the item's window where it's on a display, or the pointer; a
  rectangle's flyout is its parent's child window. They close on
  `cancelOperation:` (the content's views', a field's, the panel's), on
  `windowDidResignKey:` once the key window settles (posted), and on a
  global monitor's mouse-down outside the app's windows (an inactive
  app's windows get those events too). Click-through is
  `ignoresMouseEvents`.
- Windows: flyouts and panels are borderless popup tool windows of a
  class with a drop shadow, topmost, owned by the host's hidden window
  (a rectangle's flyout by its parent), with Windows 11's rounded
  corners; a tray flyout is placed against `Shell_NotifyIconGetRect`
  (the cursor where Windows says nothing), in the monitor's work area at
  its DPI. They take the foreground, close on an Escape the content
  leaves and on `WA_INACTIVE` to a window not their own (checked once
  activation settles, posted); a press on the tray icon leaves a flyout
  to the icon's click, which toggles it, and a click on the parent while
  a flyout has the keyboard only closes it. No system backdrop: every
  view is an opaque child window, which would hide it. Click-through is
  `WS_EX_LAYERED | WS_EX_TRANSPARENT`, opaque.
- Linux: flyouts and panels are undecorated (their header bar hidden),
  take the keyboard with `gtk_window_present` and close on an Escape the
  content leaves (a bubbling key controller on the window) and on
  `notify::is-active` going false, unless a window transient for them
  shows. GTK 4 dropped placing windows and keeping them above, so under
  X11 the host does it through Xlib on the surface's window: a utility
  window out of the taskbar and pager, `_NET_WM_STATE_ABOVE` (and
  `STICKY` for a panel) asked as EWMH says, `XMoveWindow` before it
  maps: a panel on the pointer's display a third down, a flyout against
  its parent's rectangle (`XTranslateCoordinates`) or where its tray
  icon was clicked (StatusNotifierItem's `Activate` x, y, where the
  panel says: KDE Plasma, GNOME's AppIndicator extension), in the work
  area; and with it a window's `X`, `Y`, `SetPosition`, `Center` and
  `AlwaysOnTop`. Under Wayland the compositor places an ordinary window
  and keeps it in the taskbar (an `xdg_popup` needs a parent surface,
  which a tray flyout hasn't); a panel is an overlay layer-shell surface
  where the app runs with gtk4-layer-shell (`LD_PRELOAD`: it must load
  before libwayland-client) and the compositor speaks wlr-layer-shell
  (KDE Plasma, Sway, Hyprland, COSMIC; not GNOME). A tray flyout closed
  by the click on its icon (which took its keyboard) remembers it a
  moment, so that click doesn't open it again. Fit is the content's
  size request and the default size; click-through an empty input
  region (`gdk_surface_set_input_region`), set again after each frame
  while on.
- The web: a flyout and a panel are window panels without a title bar,
  above the others, placed with a port of `Place` (a tray anchor: the
  page's top right), closed on Escape and a press outside them;
  click-through is `pointer-events: none` on the panel, the page under
  it taking the clicks.
- `asuka/sdk/apps` lists the apps installed (module "apps": "list" answers
  JSON, "icon" draws a PNG in the caches and answers its path, "open"),
  sorted and deduplicated in Go; `Search` ranks them in Go. macOS reads
  the application folders (`displayNameAtPath:`, bundle identifiers,
  `iconForFile:`, `openApplicationAtURL:`), Windows the shell's
  Applications folder (`FOLDERID_AppsFolder`, `IShellItemImageFactory`,
  `ShellExecute`), Linux the XDG desktop entries in Go (the theme's
  icons drawn by GTK, `g_app_info_launch_uris_async` with the display's
  launch context). Phones and the web answer `ErrUnsupported`.

**Displays, power, the Dock and the system's appearance** (`screen`,
`power`, `dock`, `appearance`):

- Packages over modules, not ops: what the system says about the
  computer isn't a view's. Each state the app follows is a module with
  "current", "next" and "stop" (`internal/follow`): a goroutine asks
  "next" with the state it last got, which the host answers once its
  state is another (at once for none), and hands the answer to the
  watchers on the main thread; the last watcher's stop sends "stop",
  which answers the waiting "next" with "stopped". Events (`power`'s
  sleep and lock) ride the same answer, after the state: the host
  queues them from the first "next" until "stop", so none is lost
  between two. Each package has `Watch` and `Use` (a signal, stopped
  with its owner); a host reads its state again as the platform says it
  changed, and the desktop ones as each "next" comes too. `host/fake`
  answers later as hosts do (`AsyncModules`), and `follow.Fake` is such
  a module's host side for tests.
- `screen.Displays` (Electron's `screen`): each display's ID, name,
  bounds and work area in points in the desktop's coordinates (the
  origin at the primary display's top left, y down), scale, refresh
  rate and whether it's the primary; `Primary`, `Nearest` and
  `Matching` pick one. Where displays have different scales (Windows),
  each one's origin is its pixels over the least scale of the displays
  and it goes on in its own, so displays never overlap. JSON on the
  wire, the primary first, the same bytes for the same displays (what
  "next" compares).
- `power`: whether there's a battery and the computer runs on it, its
  level and charging, Low Power Mode, the thermal state; `OnSuspend`,
  `OnResume`, `OnLock`, `OnUnlock`; `IdleTime`, since the user's last
  input in any app (`ErrIdleUnknown` where the system doesn't say).
  Keeping the display on stays `device.KeepAwake`, working out of sight
  `asuka/sdk/background`.
- `dock.SetProgress` shows a task's progress on the app's icon (Normal,
  Indeterminate, Paused, Error); `dock.RequestAttention` asks the user
  to come to the app (Informational, Critical), and does nothing while
  the app is in use. The icon's count stays `notify.SetBadge`.
- `appearance`: the system's accent color (0 where it has none),
  Increase Contrast and Reduce Transparency, beside what
  `Environment` carries (dark mode, Reduce Motion, the text size).
- macOS: the displays are `NSScreen`'s (the first has the menu bar;
  AppKit's bottom-left coordinates flipped; `visibleFrame` the work
  area; the CGDirectDisplayID the ID; `localizedName`,
  `maximumFramesPerSecond`), read again at
  `NSApplicationDidChangeScreenParametersNotification`. Power is IOKit's
  power sources (`IOPSCopyPowerSourcesInfo`: the internal battery's
  capacity and charging, what the Mac runs on; followed through
  `IOPSNotificationCreateRunLoopSource`), `NSProcessInfo`'s Low Power
  Mode and thermal state and their notifications, `NSWorkspace`'s
  sleep and wake, the lock screen loginwindow's distributed
  notifications (`com.apple.screenIsLocked`, `screenIsUnlocked`), the
  idle time Quartz's (`CGEventSourceSecondsSinceLastEventType`, any
  input). The progress is drawn on the Dock tile, under its badge: the
  tile's content view is the app's icon with a bar over its bottom, two
  `NSBox`es (an `NSProgressIndicator` doesn't draw in a tile, which
  renders offscreen), the fill the accent, red for an error, yellow
  paused; an indeterminate one is a third of the bar sliding across,
  the tile drawn again 15 times a second. Attention is
  `requestUserAttention:` (critical bounces until the app is active).
  The accent is `controlAccentColor` in sRGB, the options `NSWorkspace`'s
  accessibility display options, followed through
  `NSSystemColorsDidChangeNotification` and
  `NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification`.
- Windows: one hidden top-level window of the modules' class, never
  shown, hears what Windows broadcasts to top-level windows only (a
  message-only window hears none): `WM_DISPLAYCHANGE`,
  `WM_SETTINGCHANGE`, `WM_SYSCOLORCHANGE`,
  `WM_DWMCOLORIZATIONCOLORCHANGED`, `WM_POWERBROADCAST`; the power and
  session notifications are registered for it. Displays are the
  monitors (`EnumDisplayMonitors`, `GetMonitorInfo`), each at its
  effective DPI (`GetDpiForMonitor`), named by the display
  configuration's EDID name ("Built-in Display" for a laptop's panel
  without one, else its adapter's description), at its path's refresh
  rate; read again on a display, setting or DPI change, and once more a
  moment later, as changes settle. Power is `GetSystemPowerStatus` (Low
  Power: battery saver, Windows 11's energy saver; no thermal state),
  read again on `PBT_APMPOWERSTATUSCHANGE` and the power settings
  registered (the power source, the battery's level, the savers). Sleep
  and waking are `PBT_APMSUSPEND` and `PBT_APMRESUMEAUTOMATIC`, which
  `RegisterSuspendResumeNotification` sends on PCs with modern standby
  too, each told once; locking is `WM_WTSSESSION_CHANGE`
  (`WTSRegisterSessionNotification`); the idle time `GetLastInputInfo`'s.
  The progress is the main window's taskbar button's (`ITaskbarList3`,
  paused yellow, error red), set again when Explorer makes the button
  again (`TaskbarButtonCreated`). Attention is `FlashWindowEx` on the
  window in use: once for an informational request (the button stays
  lit until the app is used), the button and the title bar until the
  window comes to the foreground for a critical one. The accent is the
  accent palette's own entry (else DWM's colorization color), a contrast
  theme is Increase Contrast, transparency effects off Reduce
  Transparency.
- Linux: the displays are GDK's monitors (`gdk_display_get_monitors`,
  followed through `items-changed` and each monitor's geometry, scale
  and refresh rate): the geometry is in application pixels, which are
  points, the scale fractional (GTK 4.14), the ID the connector (DP-1),
  the name the monitor's description. GTK 4 has no primary monitor: on
  X11 it's the X server's and the work area the window manager's
  (`gdk_x11_monitor_get_workarea`; a panel moving has no signal of
  GDK's, so it's seen at the next change); on Wayland the compositor
  keeps both, so the work area is the bounds and the primary is GNOME's
  (Mutter's `DisplayConfig`, read again on `MonitorsChanged`), else the
  monitor at the layout's origin. Power is UPower (`OnBattery`, the
  display device's presence, percentage and charging), GIO's power
  profile monitor (the power saver profile, through the portal in a
  sandbox), logind's `PrepareForSleep` for sleep and wake, and the
  session's `LockedHint` with the screen saver's `ActiveChanged`
  (GNOME's, KDE's) for the lock, one locked flag between them; the idle
  time is Mutter's idle monitor's, else KDE's screen saver's, else not
  known; no thermal state. The progress is the launcher API's
  (`com.canonical.Unity.LauncherEntry`, as notify's badge, each
  `Update` the whole entry: count, progress, urgent), which Dash to
  Dock, Ubuntu's dock and Plasma's task manager draw (indeterminate a
  full bar, paused and error as normal); GNOME's own dash shows none.
  Attention sets the entry's urgent flag and, on X11, the window's
  urgency hint; on Wayland, where GTK 4 has no urgency, it presents the
  window without an activation token, which GNOME and KDE don't grant
  but show as a notification that the app is ready, and which can't be
  taken back. It ends as one of the app's windows becomes active. The
  appearance is the settings portal's `org.freedesktop.appearance`
  (`accent-color`, `contrast`, followed by `SettingChanged`),
  libadwaita's high contrast without it; Linux desktops have no Reduce
  Transparency.
- The web: one display, `window.screen`'s (its work area `availLeft`,
  `availTop`, `availWidth`, `availHeight` where the browser gives them,
  its scale `devicePixelRatio`), or every display, with their places,
  labels and which is primary, from the Window Management API
  (`getScreenDetails`, Chrome) once the user has granted it: the host
  never asks. A page isn't told a refresh rate (0). Power is the Battery
  Status API (Chrome), which reports a computer without a battery as
  one charging, full and never discharging: read as none. A page is told
  of no sleep, lock, Low Power Mode or heat, and not the idle time (the
  Idle Detection API asks the user). The accent is CSS's `AccentColor`
  where the browser has it, the options `prefers-contrast: more` and
  `prefers-reduced-transparency: reduce`. A page has no icon: `dock`
  does nothing.
- Phones answer with what they have. The displays are the device's own
  screen first, then any it mirrors or extends to (AirPlay, a cable,
  Android's secondary displays), in a row after it, each in points as
  it's turned now, its work area its bounds (the bars are the app's
  safe area). Power is `UIDevice`'s battery or the sticky battery
  broadcast, Low Power Mode or Battery Saver, the thermal state or
  status; the screen locks as protected data goes (iOS, with a
  passcode) or as it goes off on a secure lock screen (Android), and
  unlocks as protected data comes back or at the user-present broadcast.
  A phone suspends an app as it goes to the background, not as it
  sleeps: no Suspend or Resume, and no idle time. iOS has no system
  accent; Android's is Material You's, as the system's dynamic primary
  (accent1 at tone 40 light, 80 dark), its contrast the contrast level
  (Android 14) or high contrast text, no Reduce Transparency. `dock`
  does nothing.

**Files, one instance, quitting, recent documents and login**
(`ui/call.go`, `app/quit.go`, `app/dirs.go`, `recent`, `login`):

- `asuka.toml`'s `fileTypes` (name, extensions, MIME types, UTI,
  `conformsTo`, role, icon) are declared by each build
  (`cmd/asuka/filetypes.go`): macOS and iOS `CFBundleDocumentTypes` by
  UTI, the app's own types (a UTI under its ID) exported, others the
  system may not know imported, iOS opening in place; Android
  `ACTION_VIEW` filters; Windows a ProgID per type and the extensions'
  `OpenWithProgids` (and `Applications\<exe>\SupportedTypes`), which the
  host writes under `HKCU\Software\Classes` as it starts, from
  `boot.FileTypes` (internal/filetype, `-ldflags -X`), the icons in the
  executable; Linux the desktop file's `MimeType` (and `%U`), a
  shared-mime-info package for the app's own types and their icons; the
  web manifest's `file_handlers`.
- Files reach the core with no event of their own: `IncomingURL`'s
  string carries `file://` URLs, one per line (internal/fileurl; a URL
  has no raw line break), so the files opened together arrive together.
  `ui.incoming` splits them: the paths go to `ui.OnOpenFiles`, the other
  URLs to `ui.OnOpenURL`; both are owner-scoped and keep what came before
  any handler for the first. macOS: `application:openURLs:` (Finder,
  Open With, the Dock icon, the File menu's Open Recent). Windows and
  Linux: the command line's files that exist (taken from the launch's
  working directory) and `file://` URIs, with the scheme's URLs
  (`fileurl.FromArgs`); Linux's GApplication `open` too. The web: the
  launch queue's files, read into the app's caches.
- One instance: an app with an ID runs once on Windows (a mutex of the
  session; the second launch sends its working directory and arguments
  through `WM_COPYDATA`) and Linux (a unique GApplication with
  `HANDLES_COMMAND_LINE`: the second launch's command line over D-Bus),
  except under automation and `asuka dev`, or with `"singleInstance":
  false` (`boot.MultipleInstances`). The running one comes to the front,
  `app.OnSecondInstance(args, dir)` gets the arguments, and their files
  and URLs come as above. macOS's Launch Services runs an app once by
  itself, and passes files as Apple Events, not arguments.
- Quitting: `app.Quit` (`boot.Quit`, set by a desktop host) quits as the
  user's Quit does. Every quit asks `boot.QuitRequested` (package app:
  `app.OnQuit`'s handlers, with a `QuitEvent` they can `Cancel`) once;
  one they allowed only finishes. macOS asks in
  `applicationShouldTerminate:` (⌘Q, the Dock, the last window, SIGINT
  and SIGTERM, which send `terminate:`), with `SessionEnding` after
  `NSWorkspaceWillPowerOffNotification`; Windows on the main window's
  `WM_CLOSE`, Ctrl+C and `WM_QUERYENDSESSION`; Linux on the last
  window's `close-request`, SIGTERM, SIGINT and SIGHUP, and `query-end`.
  The session's end can't be canceled; a second signal exits at once.
  Phones and the browser never quit themselves (`boot.Quit` is nil).
- `app.DataDir`, `ConfigDir`, `CacheDir` and `LogDir` are
  internal/appdir's, each platform's place for the app (`store.Dir` is
  DataDir).
- `asuka/sdk/recent` (module `recent`: `add`, `clear`, `list`):
  NSDocumentController's recent documents on macOS, in an Open Recent
  menu the host fills as it opens (in the File menu once the app
  declares file types, has recent documents or adds one; choosing one
  goes through `openURLs`); on Windows `SHAddToRecentDocs` (the jump
  list's Recent) and the app's own list in its data directory, cleared
  with `IApplicationDestinations::RemoveAllDestinations`; on Linux the
  user's `recently-used.xbel`, through GBookmarkFile, as the app's (its
  application name), clearing only the app's. Phones and the web answer
  nothing.
- `asuka/sdk/login` (module `login`: `set`, `enabled`, `openedAtLogin`):
  SMAppService's `mainAppService` on macOS 13 (a bundle only;
  `requiresApproval` is `ErrNeedsApproval`), the opening Apple Event's
  `keyAELaunchedAsLogInItem` for `openedAtLogin`; Windows the HKCU `Run`
  value (and Task Manager's `StartupApproved` for `enabled`); Linux an
  XDG autostart entry, or in a Flatpak the Background portal's
  `RequestBackground`. The Windows and Linux entries start the app with
  `boot.LoginArg`, which the host takes out of `os.Args` in its init.

**Global shortcuts** (`hotkey`): keys that reach the app while another
app is in front (a launcher's panel, start and stop a recording).

- `hotkey.Register(Props{Shortcut, Description, OnPress})` registers a
  `ui.Shortcut` while the calling component's owner lives; disposed (or
  `Unregister`), the keys are let go. The shortcut follows its accessor:
  the old keys go as the new ones are taken, and a Shortcut without a
  Key registers nothing (a setting the user cleared). `OnPress` runs on
  the main thread at each press; a key held down doesn't repeat it, and
  only the press is reported (Windows tells no release).
- `Status()` (off, pending, active, failed) and `Err()` are signals:
  `ErrTaken` (another app has the keys, or this one already does),
  `ErrNotAllowed` (the user declined, or the desktop won't bind them for
  an app it can't place), `ErrNotAvailable` (phones, the web, a Wayland
  desktop without the portal), `ErrKey` (no such key).
- One module, `hotkey`: `register` (id, modifiers, key, description),
  `unregister`, and a long poll, `next`, answered with the ids pressed
  since; one runs while any shortcut is active, `stop` ends it. Phones and
  the web have no module, which the package reports as `ErrNotAvailable`.
- macOS: Carbon's `RegisterEventHotKey` through purego, as every Mac
  app's global shortcut (no Accessibility permission), registered
  exclusively; the system's own shortcuts (`CopySymbolicHotKeys`) are
  taken. A character is the key that types it on the keyboard's layout
  (`UCKeyTranslate`). Another app's non-exclusive registration of the
  same keys isn't reported: both get the press.
- Windows: `RegisterHotKey` on a message window, `MOD_NOREPEAT`; it fails
  while another app has the keys. The press lets the app take the
  foreground. Keys map as the menu bar's accelerators do.
- Linux, Wayland: apps can't see other apps' keys; the desktop binds
  them through the XDG desktop portal's GlobalShortcuts (GNOME 48, KDE
  Plasma 6, Hyprland). A session binds its shortcuts once, so each change
  binds them all in a new session (named after the app), with their
  descriptions and preferred triggers; the desktop may ask the user to
  confirm or pick other keys, remembers them by the shortcut's id, and
  knows the app by its desktop entry. `Activated` is a press; its
  activation token is the display's while `OnPress` runs, so a window
  it shows takes the focus. Linux, X11: `XGrabKey` on the root window
  (in Caps Lock's and Num Lock's states too), taken while another client
  has the keys; presses come through GDK's `xevent`. Neither (GNOME
  before 48, XWayland alone, whose grabs see only X11 windows' keys):
  `ErrNotAvailable`.

**File dialogs and the file manager** (`dialog`, `shell`):

- `dialog.Open`, `dialog.Save` and `dialog.Folder` give the user's own
  paths, not copies (`device.PickFiles` gives copies, as a phone's
  document picker does): the app reads and writes the files where they
  are. Their options are a title, the button's label, where the dialog
  starts (a folder, or a file in it: selected on macOS and Windows, or
  its name suggested), filters (`dialog.Filter`: MIME types as an
  input's accept takes them, `image/*` a family, or extensions, or
  both), several files or folders, hidden files, and `From`, a view in
  the window the dialog belongs to (default: the one in use). They're module calls (`dialog`:
  `open`, `save`, `folder`, the options as JSON), answered with the
  paths (JSON) or the path once the user has chosen; nothing is a
  cancel.
- macOS: `NSOpenPanel` and `NSSavePanel` as sheets on the window (on
  the sheet it has, if it has one: a sheet on a window with one waits
  for it to close), the title the panel's message (a sheet has no
  title bar), the filters `UTType`s (a Mac's panel has no type menu: it
  takes any of them; an extension no app declares is a dynamic type of
  its own); with no window on screen, a panel of its own. Windows:
  `IFileOpenDialog` and `IFileSaveDialog` (`FOS_PICKFOLDERS` for
  folders), modal to the window, run once the core has returned (a
  modal loop); a filter's types are its extensions and its MIME types'
  (the registry's; a family's are the ones Windows's own apps open),
  and a name typed without an extension gets the chosen filter's.
  Linux: `GtkFileDialog` (`open`, `open_multiple`, `save`,
  `select_folder`, `select_multiple_folders`), modal to the window; in
  a sandbox it's the desktop's file chooser portal, and the paths are
  the document portal's. Each asks before Save replaces a file.
- The web has no paths: a browser gives a page files, never where they
  are. `Open` and `Folder` copy what's picked into the app's caches
  (`/tmp/shared`, `/tmp/folders`: a folder with all it holds), through
  the File System Access API's dialogs (`showOpenFilePicker`,
  `showDirectoryPicker`: Chrome, Edge) or, without them, the file input
  (`webkitdirectory` for a folder). `Save` gives a path in the caches
  (`/tmp/saved/<n>/<name>`) whose file, each time the app has written
  it (closed a file it opened to write, or renamed one over it: fs.js's
  `onWritten`), goes to the file the user chose with
  `showSaveFilePicker`, or is downloaded by its name where the browser
  has no such dialog. A path a dialog gave is where a later one starts
  (`startIn`, the handle behind it).
- Phones: iOS and Android have no such dialogs. `Open` is the document
  picker, through `device.PickFiles` (a phone's host has no `dialog`
  module: its "no module method" answer sends the package there), and
  gives its copies; `Save` and `Folder` are `dialog.ErrUnsupported`.
- `shell.Reveal` shows a file selected in its folder: Finder
  (`activateFileViewerSelectingURLs:`), Explorer
  (`SHOpenFolderAndSelectItems`, on a thread of its own: a window already
  showing the folder is used), the file manager's
  `org.freedesktop.FileManager1.ShowItems` over D-Bus (Files, Dolphin,
  Nemo, Thunar; without one, the folder opens). `shell.Open` opens a
  file in its app and a folder in the file manager (`NSWorkspace`,
  `ShellExecuteEx`, `GtkFileLauncher`; `ErrNoApp` for a type no app
  opens, though Windows asks first, as Explorer does); on the web,
  preview's new tab. `shell.Trash` moves an item to the trash and never
  deletes: `NSFileManager`'s `trashItemAtURL:` (off the main thread),
  GIO's `g_file_trash_async`, which fail on a volume without a trash,
  and on Windows `IFileOperation` with `FOFX_RECYCLEONDELETE` (on a
  thread of its own) and a progress sink whose `PreDeleteItem` refuses a
  delete that wouldn't recycle (too big for the Recycle Bin, a drive
  without one), which Windows would otherwise do without asking.
  `shell.Beep` is `NSBeep`, `MessageBeep(MB_OK)`, `gdk_display_beep`.
  Phones and the web have no file manager: Reveal and Trash are
  `shell.ErrUnsupported` there (Open too on phones: `preview.Show`
  shows a file), Beep nothing.

**Watching files** (`watch`): changes to a file or a directory, and with
`Recursive` the directories in it; every platform, phones and the web
too. MyGo's `WatchFiles`, ported.

- No module and no host: the package calls each system from Go, in
  the app's process (on iOS and Android, the Go library's), without
  cgo. `watch.New(ctx, path, Options)` from any goroutine, `Next` for
  each batch, `Close`; `watch.Path(path accessor, Options, f)` in a
  component: the watch starts on a goroutine, follows its accessor,
  stops with its owner, `f` runs on the main thread (`ui.Post`), and the
  next batch waits until it has; `Err()`, `Watching()` and `Root()` are
  signals.
- A watch is a snapshot and a comparison, not a relay of the system's
  events: it keeps each entry's identity (device and inode; volume and
  file ID on Windows, read when the stat is taken), size, modification
  time and mode, and where the system says something changed (a
  notice: a directory, or an entry in one, with a kind that's only a
  hint), it lists or stats again, once the notices stop for `Debounce`
  (50 ms; never more than `MaxDelay`, 500 ms). The differences are the
  events: `Create`, `Write`, `Remove`, `Rename` (paired by identity when
  exactly one entry has it before and after: any platform, any
  distance within the tree; a directory renamed is one event),
  `Attrib` (the mode, with `Attributes`), and `Overflow` first in a
  batch the system dropped changes for, after which it looked at
  everything. A file replaced at its path by another file (an atomic
  save) is a `Write`, never a removal and a creation, and the new file
  is watched. A file is watched through its directory. The watched
  directory removed, renamed or replaced ends the watch with a
  `Remove` of `.`. The watch doesn't look again until `Next` took the
  last batch, so a slow reader gets one batch of what changed meanwhile;
  notices that come while it looks wait (spilled, up to `MaxEntries`,
  then a look at everything).
- It never leaves the tree: entries are stat'ed and directories
  opened relative to their directory (`os.Root`), symlinks and
  junctions are entries, not followed, and each target the system
  watches is checked to be what the scan saw. What it can't read is
  left out, not an error; `Ignore` (`watch.Patterns`, gitignore-like)
  leaves out entries, and a directory it ignores is never opened.
  `MaxEntries` (65536 on macOS, 8192 elsewhere) bounds a tree.
- macOS: FSEvents through purego, a stream per directory target with
  `kFSEventStreamCreateFlagFileEvents` and `WatchRoot`, on one serial
  dispatch queue, one callback for the process; a stream covers its
  tree, so directories below need none. Dropped events
  (`MustScanSubDirs`, user or kernel drops) are an overflow.
- iOS: kqueue's `EVFILT_VNODE` on an `O_EVTONLY` descriptor per
  directory and file, an `EVFILT_USER` event to wake it (macOS runs it
  too, and the tests run every case through both).
- Linux and Android: inotify, one instance for the process (watch
  descriptors, not instances, are what a user has many of), a watch
  descriptor per directory, `IN_Q_OVERFLOW` an overflow. inotify says a
  directory was removed (`IN_DELETE_SELF`) only once nothing holds it
  open, and the watch does: a directory's watch watches its parent too,
  which says so at once, when it may.
- Windows: `ReadDirectoryChangesW` on a completion port, one handle for
  a recursive tree (its directories stay renamable), its parent's too
  (a directory's own handle says nothing when it's renamed), the
  buffer's overflow (`ERROR_NOTIFY_ENUM_DIR`, or nothing returned) an
  overflow. `FILE_NOTIFY_CHANGE_ATTRIBUTES` only with `Attributes`.
- The web: Go's `os` on js/wasm calls the page's `fs`, fs.js's file
  system in memory, whose `watch` is Node's `fs.watch` (rename and
  change, by name, recursive), told after the call that changed it;
  its entries have inode numbers of their own, kept as they're renamed.
  Under Node (`go test`), Node's `fs.watch`.

**Now Playing and the media keys** (`nowplaying`), on every platform
(phones first): what the app plays in the system's media controls, and
their commands back.

- `nowplaying.Show(Props)` in a component: accessors for the title,
  artist, album, artwork (a source, as `Image`'s), duration, state
  (`Paused`, `Playing`, `Stopped`), position and rate, read in one render
  effect; handlers for play, pause, toggle, stop, next, previous, seek
  (to a position) and the skips (by `SkipInterval`, 15 s). A handler
  enables its command; play, pause and toggle stand in for each other;
  seek needs a duration. Several mounted: the last shows (a stack). The
  last one disposed clears the controls once the flush is over (a posted
  check), so a component replaced by another (a track's, in a `Dynamic`)
  doesn't drop the app out of them in between.
- The position is published with the state and the rate, and the host
  takes its time on receipt, so the system advances it; the package
  sends it again only when it's half a second off where the system has
  it while playing (a seek, a stall), or changes while paused. A
  position signal written from a polled status costs nothing.
- `Props.Player`, an `*audio.Player`: its `Status` is read once a
  second (a timer, `ui.Post`) and fills the state, position and
  duration; play, pause, stop, seek and, with a `SkipInterval`, the
  skips run on it unless their handlers are set, and show at once (the
  status assumed until the next read, which a read already in flight
  can't overwrite).
- One module, `nowPlaying` (internal/nowplayingspec): `set` (the info as
  JSON, with the enabled commands as bits), `clear`, and a long poll,
  `next`, answered with the commands since (u8 command, f64 value each);
  `stop` ends it. Hosts drop commands while no poll follows, or that
  aren't enabled.
- iOS and macOS: `MPNowPlayingInfoCenter` (the playback rate 0 while
  paused; `playbackState` on the Mac) and `MPRemoteCommandCenter`'s
  commands, their targets added once, enabled per `set`; the artwork an
  `MPMediaItemArtwork` of the image loaded off the main thread (on the
  Mac its handler decodes the bytes it holds, so the image goes with the
  artwork). iOS puts the audio session in the playback category; the CLI
  adds the `audio` background mode for an app that imports
  asuka/nowplaying (its manifest's `backgroundModes`). The Mac's through
  purego (host/darwin), which also routes the keyboard's media keys.
- Android: a platform `MediaSession` (its `PlaybackState` actions the
  commands; the skips also custom actions, which fill the previous and
  next slots of Android 13's media controls), with a `MediaStyle`
  notification; `AsukaMediaService`, a foreground service of the
  `mediaPlayback` type, runs while playing and detaches the notification
  when paused (Android 14's rules; a start the system refuses in the
  background leaves the notification alone). The build declares it and
  its permissions only for apps that import the package.
- Windows: `ISystemMediaTransportControlsInterop::GetForWindow` on the
  app's window (or a hidden one of the host's), its buttons (fast
  forward and rewind the skips) and timeline, delegates that post to
  the main thread; the artwork read off it into a stream. Linux: an MPRIS
  player (`org.mpris.MediaPlayer2` and `.Player`) on a session bus
  connection of its own, its name
  `org.mpris.MediaPlayer2.<app>.instance<pid>`; `Position` extrapolated
  as it's read, `Seeked` on jumps, a new track id when the track
  changes, `Seek` a skip or a seek; `clear` closes the connection, so
  the name goes. The web: the Media Session API (`metadata`,
  `playbackState`, action handlers, `setPositionState`).

**Bluetooth LE** (`bluetooth`), on every platform: the app as a central
(scans, the system's chooser, connections, GATT reads, writes and
subscriptions) and as a peripheral (advertising, a GATT server).

- Lasting things start on the main thread and stop with their owner, or
  their stop: `Scan`, `Connect` (a `Connection` at once, `Connecting`; its
  `State` a signal, `Wait`, `Done` and `Err` for goroutines),
  `Characteristic.Subscribe`, `Advertise`, `Serve`, `Watch`/`Use`,
  `OnRestore`. Calls that wait for a device or the user (`Request`,
  `Choose`, `Services`, reads, writes, MTU, RSSI) are blocking, off the
  main thread; a connection's wait for it to connect first. An error
  ends a callback's stream once (`func(v, err)`).
- One module, `bluetooth` (internal/bluetoothspec, the wire's reference):
  JSON both ways, byte values base64, UUIDs 128-bit lower case. Go
  numbers scans, connections, advertisements, servers and their local
  characteristics; hosts number the remote attributes (stable handles
  per connection). Events (advertisements, disconnections with their
  reason, notifications, the server's requests, state changes) come
  through one long poll, `next`, chained from the main thread (a Call's
  callback calls again) while anything listens; `stop` when nothing
  does. Several connections to one device share the platform's link.
  host/fake implements the protocol whole, with simulated peripherals,
  for uitest.
- iOS: Core Bluetooth, compiled in for an app whose archive calls the
  package (`ASUKA_BLUETOOTH`, through its `call` and `send`); state
  restoration when the app has the background modes (asuka.toml's
  `bluetooth.background`), the managers made at launch for a relaunch.
  macOS: the same through purego. Android: one merged scan, one GATT
  operation at a time per connection, the companion device chooser, a
  `PendingIntent` background scan whose results wait in a file for
  `restore`. Windows: WinRT's Bluetooth, GATT and advertisement APIs, the
  device picker. Linux: BlueZ on the system bus, the app's advertisement
  and GATT application exported objects. The web: Web Bluetooth, the
  chooser instead of a scan; no peripheral role (`not available`).

**Updates** (`asuka/update`, std/update; core/src/std/update.c,
inflate.c, ed25519.c; the hosts' `update` module; `internal/update` and
`cmd/asuka/updates.go` for the CLI): a desktop app updates itself as
Sparkle's do, from releases `asuka build` signs; MyGo's updater, ported.
The package is Asuka, written once; the core does what Asuka shouldn't
over files, as its `native fn`s; the host does what only it can.

- The format (`internal/update`, the CLI's): a manifest per target
  (`darwin-universal`, `windows-amd64`, `linux-arm64`...),
  `update-<target>.json`, or a prerelease's channel's
  `update-<target>-beta.json` (a version's channel is its prerelease's
  letters): the version, Markdown notes, the date, the archive's URL,
  size and Ed25519 signature of its SHA-256, the deltas from earlier
  versions and the archives before (for the next build's deltas).
  Versions compare as semantic versioning orders them. An archive is a
  gzipped tar of the app as installed; a delta (Sparkle's kind) lists the
  new app's tree, each file a copy of the old app's (by path or by
  content), a bsdiff patch of it (suffix array by qsufsort), or new
  (DEFLATE), and every file it makes is checked against its size and
  SHA-256, so a delta applied to a changed app fails and the archive is
  downloaded instead. The CLI writes them (WriteArchive, WriteDelta,
  Diff, Sign); internal/update's TestCoreTestdata writes the core's test
  data with that code, so the core reads what the CLI writes.
- The core (`update.*`, blocking, called in spawn): `verifyFile` (the
  file's SHA-256 streamed, Ed25519 verified, strict as Go's:
  ed25519.c, TweetNaCl's arithmetic; asuka/crypto has the same
  `ed25519Verify`, `ed25519Sign`, `sha512`), `extract` (gzip and tar
  read as streams: ustar, PAX and GNU names, files, directories, links),
  `deltaIndex`, `deltaStart` and `deltaFile` (a file inflated, copied or
  bspatched; DEFLATE pulled from three sections of the delta at once),
  `makeDir`, `makeLink`. Nothing is written through a link or out of
  the tree (a path whose parent is a link or a file is refused; a link's
  target, read from where it is, stays inside). Tested under ASan with
  the CLI's archives, deltas and patches, cut and flipped.
- The package (std/update): `check()`, `Release.install(progress:)`,
  `relaunch()`, `unsupported()`, `setFeed`, `setChannel`,
  `compareVersions`; the updater's state, the store `update.updater`
  (phase, release, downloaded, total, error, user, installing);
  `checkForUpdates()`; automatic checks and downloads, the skipped
  version and the last check as preferences (asuka/storage's
  `asuka.update`); `onInstalled`; `UpdateWindow` (its tasks run what's
  asked and the next background check: soon after launch, then an
  interval after the last that succeeded, waking at least hourly to
  compare with the clock), `CheckForUpdates` (the menu item) and
  `AppMenu`. The feed: a static manifest URL (a channel's beside it, the
  newer taken), GitHub's latest release, or with a tag prefix or a
  channel the newest tagged release the API lists, or a server's URL with
  `{target}`, `{version}` and `{channel}` that answers a manifest or 204.
  Everything is read through the host's `download` (manifests and API
  pages too, into the app's directory): HTTPS, or HTTP to this computer;
  429 and 5xx tried three times. Installing: a work directory in `work`
  (`.<Name>.app.update-<id>` for a bundle, `.asuka-update-<id>`
  otherwise), the delta from the running version downloaded, verified
  and applied there from `root` (its tree the bundle's or `<exe>/`'s, as
  an archive unpacks), else the archive downloaded, verified and
  unpacked; a folder's and a prefix's tree must hold the running
  executable's counterpart; then the host's `install`. What fails is
  removed. The window's words are internal/l10n's, copied into
  std/update/words.asuka (internal/l10n's TestUpdateWords), in the app's
  language (`i18n.locale()`).
- **The host's `update` module**, JSON both ways through asuka/platform's
  `call`, each method answering once. A host without it (the phones, the
  web, the Go macOS host) answers "no module": the package says
  updates aren't available (`.notAvailable`).
  - `info` → `{installable, reason, message, target, version, name,
    feed, key, kind, root, exe, work}`. `feed`, `key` and `target` are
    asuka-app.json's `updateFeed`, `updateKey` and `updateTarget` (only
    `asuka build`'s builds have them); `version` and `name` asuka.toml's.
    `kind` is `bundle` (macOS: `root` the .app), `folder` (Windows: the
    folder holding the .exe and its assets, with `.asuka-files`) or
    `prefix` (Linux: `root` the prefix of `bin/<exe>` and
    `share/<exe>/`); `exe` the running executable, links resolved (on
    Linux, the path it started from: `/proc` names the replaced file);
    `work` where updates are made, on root's volume (a bundle's parent,
    the folder, `share/<exe>`). Not installable, `reason` says why:
    `notConfigured` (no feed or key), `managed` (the Mac App Store's
    receipt, MSIX's `WindowsApps`, Flatpak, Snap, AppImage, `/usr`),
    `translocated` (App Translocation), `notWritable` (a probe in `work`,
    and the bundle's own entry, can't be made), `notInstalled` (not a
    layout `asuka build` makes); `message` in words.
  - `download {url, to}` → `{id}`, at once: a GET of `url` (redirects
    followed, the environment's proxy, `User-Agent: asuka-updater`) into
    the file `to`, made or emptied. WinHTTP, libsoup through GIO,
    URLSession.
  - `next {id}` → `{downloaded, total, done, error, status}`: a long
    poll, answered when `downloaded` has moved by at least 1% of `total`
    (64 KB while `total`, the Content-Length, is 0: unknown) or the
    download has ended (`done`). `status` is the response's, once known
    (a body is kept for 200 only); `error` a failure of the network or
    the file ("" when none). After `done` the id is forgotten.
  - `cancel {id}` → `{}`: stops it; a `next` waiting answers `done` with
    `error` "canceled".
  - `install {staged}` → `{}`, or an error: `staged` is a directory in
    `work`, the new version's tree (the bundle; the folder's entries; the
    prefix's tree), which the package has verified. macOS: the bundle's
    code signature is checked (`codesign --verify --deep --strict`, the
    running app's team ID and identifier when it's signed; the
    quarantine attribute taken off) and it's left to a helper,
    `/bin/sh` in a session of its own, that waits for the app's pid to
    exit, swaps the bundles (the old one back if the new can't be moved
    in), registers it with Launch Services and, when the app relaunched,
    opens it with `/usr/bin/open` (after the app quits, so the Dock keeps
    its tile); its work directory holds `staged`, the helper's pid. A
    second install replaces the first's. Windows and Linux: the units of
    the tree (a folder's entries; a prefix's files and links) replace the
    installed ones now, those the old version's `.asuka-files` names and
    the new lacks go, what no list names stays (the user's files, other
    programs of `~/.local`); everything is moved aside first and put back
    if one rename fails; the new `.asuka-files` is written with them. The
    running `.exe` is renamed aside (Windows lets it) and removed at the
    next launch. Linux then runs the desktop's cache tools
    (`update-desktop-database`, `update-mime-database`,
    `gtk4-update-icon-cache`). internal/update's install.go and bundle.go
    at bf1839e2 are the Go reference.
  - At launch the host removes what an update left: a bundle's
    `.<Name>.app.update-*` whose helper isn't running, and elsewhere
    `.asuka-update-*` and the `.*.old-*` set aside.
  - `relaunch()` (the Quit command's relaunch) quits as `quit()` does and,
    once the quit goes on, starts the app after this process exits: on
    the Mac the staged update's helper opens the new bundle (or a
    detached shell that waits, then `open`s it); elsewhere the
    executable, with `ASUKA_RELAUNCH_WAIT`, which the new process waits
    on before it takes the single instance.

  The fake host has the module only once a test sets it up
  (`update.fakeHost`, core/host/fake/update.c): its `fake`, `serve` and
  `log` methods say what `info` answers, what a URL downloads (a file's
  bytes, quarter by quarter, or a status, an error, a download held until
  cancelled) and what it downloaded and installed.
- The web: the page's version is the host's (`asuka.native.app.version`,
  "" under asuka dev); the build writes `version.json` (`Cache-Control:
  no-cache`), which the package's web side (std/update/web) reads past
  caches; a newer version is a release with nothing to download, and
  `relaunch()` reloads the page.
- The CLI: `asuka update keygen`; `asuka build` writes `dist/updates`
  (the Linux tarball is the archive; a Windows folder and a Linux prefix
  get `.asuka-files`) and the feed, key and target into asuka-app.json;
  deltas from the published manifest's versions (downloaded and
  verified); `asuka publish --platform mac|windows|linux` uploads to the
  tag's GitHub release (`gh`) or S3 (SigV4 in Go), the manifests last.
  cmd/asuka's TestUpdatesVerifiedByCore checks that what it signs, the
  core verifies, unpacks and applies.

**Documents** (`document`): document-based apps, as NSDocument and
NSDocumentController, SwiftUI's DocumentGroup and MyGo's documents make
them, built on the desktop's own pieces (`ui.Window`, `MenuBar`,
`OnOpenFiles`, `app.OnQuit`, `recent`, `dialog`) rather than ops of
their own.

- `document.New(Options[T]{Types, Read, Write, View, ...})` describes
  one kind of document whose content is a `T`; `Run` runs the app with
  it (`app.RunWindow` with the main window hidden), `Root` is its root
  for an app that runs itself. A `Document[T]`'s content is a signal
  (`Content()`): the view binds it (a text field's `Value`), and an
  effect owned by the document marks it edited as it changes, unless
  `Equal` finds it equal to the file's. Reading the file (open, revert,
  another app's change) sets it without editing. A mutable content says
  `Changed()`, and `Snapshot` copies it before a write.
- Everything is on the main thread. Each document's reads and writes are
  jobs run one at a time: a job starts on the main thread (where it
  takes its snapshot of the content), works on a goroutine, and comes
  back with `ui.Post` before the next starts, so no lock guards a
  document. Questions are callbacks (alerts, dialogs): a close or a quit
  that must ask is refused, then done again once answered.
- Files are written as NSDocument and GIO write them: a sibling file,
  flushed, renamed over the old one (its permissions, owner and group
  kept; on a Mac its extended attributes, Finder's tags; on Windows
  `ReplaceFileW`, which keeps its security, streams and creation time);
  a file with other hard links, or in a folder the user can't write,
  in place. Before writing, the file's stamp (modification time, size)
  is compared with what the document read: a changed file puts it in
  conflict instead.
- Desktop: one `ui.Window` per document (`For` over the open ones), its
  `StateKey` the file's, with the `Edited` and `File` props (the
  Window kind's): a Mac's `setDocumentEdited:` and `setRepresentedURL:`,
  a `*` before the title on Windows, Linux and the web, the file's
  folder as a GNOME title's subtitle; the title is the name, and "name -
  App" on Windows and Linux. `WindowMenuBar` (a `WindowConfig` flag)
  gives the window the app's menu bar where it's per window (Windows'
  HMENU, Linux's main menu). The File menu is the package's `MenuBar`
  (with `Options.Menus` beside it), its items enabled for the document
  in use (the window focused last); two `MenuRole`s are the hosts':
  `MenuRoleClose` closes the window in use as its close button does
  (`performClose:`, `WM_CLOSE`, `gtk_window_close`), and on a Mac
  replaces the host's Close Window; `MenuRoleOpenRecent` is a Mac's
  NSDocumentController submenu, in its place, and elsewhere the item's
  submenu, which the package fills from `recent.List`.
- Autosave (`AutosaveInPlace`, the default): a document with a file it
  can write is saved a moment after the edits stop (5 s, at most 30 s
  apart), when its window loses the focus and when it closes; others
  (untitled, read-only, in conflict, deleted, or `AutosaveOff`) keep a
  recovery copy a second after the edits stop, in `Recovery/<kind>` of
  `app.DataDir`, in a session a lock file says is alive. A launch claims
  the sessions no instance holds and opens their documents, edited.
- Closing: an unedited document closes; an edited one is saved in place
  or asks (`OnCloseRequest` false; the answer `Destroy`s the window).
  Quitting (`app.OnQuit`): documents are saved in place or kept in their
  copies, the quit canceled meanwhile and asked for again; with
  `AskOnQuit`, a Mac asks whether to review them, then each, Windows and
  Linux in one question naming them. The session's end saves at once,
  on the main thread. A Mac's app keeps running with no window
  (`boot.KeepRunning`) and its Dock icon makes a document
  (`boot.Reopen`, which `applicationShouldHandleReopen:` asks); Windows'
  and Linux's quit with their last document.
- Files other apps change: each open file is watched (asuka/sdk/watch's
  watch of it, through its folder, so a file an editor replaces stays
  watched; where there's none, or once its folder went, a poll of its
  stamp every two seconds) and looked at when its window
  comes to the front: an unedited document reads it again, an edited one
  is in conflict; a file gone is looked for by its identity (a Mac:
  anywhere on its volume, `/.vol/<dev>/<ino>` and `F_GETPATH`; elsewhere
  its folder, by inode or Windows' file index), else the document is
  Deleted.
- Phones and the web: one document at a time in the root's `Stack`: a
  list of the app's documents (its documents folder: iOS's container's
  Documents), and a document's screen pushed over it. Every document has
  a file from the start, is saved in place (also as the app goes to the
  background), and closes as its screen goes; files from elsewhere are
  copied in; a crash's copies are written back quietly (beside the file,
  "(Recovered)", where it changed).

## The web

The core is WebAssembly (`asuka.wasm`, built by internal/cbuild with
clang or Zig for `wasm32-wasi`), with the app compiled in for a release
or loaded as bytecode under `asuka dev`; the page's DOM is its views. As
the mobile hosts, the host is the platform's language, JavaScript
(`platform/web/src`, plain ES modules the CLI ships as they are), and
core/src/abi/web.c is the core's side:

- `apply` hands the host the batch's address and length in a small
  buffer both sides share (`ak_web_io`); the host decodes it in place
  out of the WebAssembly memory with `ops.js` (a call back into the core
  meanwhile may grow the memory, which detaches the old buffer, so it
  reads through the current one). `measure` passes its arguments and
  answer in the same buffer, so nothing is allocated per measure. Events
  go in encoded (`asuka_event`), as the mobile hosts send them; `post`
  asks the page for a task (`MessageChannel`), and timers are the
  page's (`timer_set`, `timer_clear`). What the C library calls of WASI
  (the clock, randomness, the console, files) `wasi.js` answers.
- The page has one thread, which is the core's main thread: the core
  runs in the calls the page makes into it (events, list requests,
  posted work, timers). The host never calls into the core while the
  core is calling it: events raised meanwhile (a field's focus as the
  core focuses it) and work that would call back (a list tiling) wait
  until the core returns, as on the other hosts.
- `spawn` runs on the page's Web Workers (`spawn.js`): up to four (the
  browser's cores less the page's), started as `spawn` first needs one,
  each an instance of the same compiled `asuka.wasm` (the module posted
  to it) with the same app (asuka dev's bytecode, which a reload sends
  again, or the app compiled in), whose init runs there without a UI
  (`asuka_worker_start`: apply and measure go nowhere, natives aren't
  there) and makes only the file's values a spawn can use: state,
  memos and what isn't a value (a database, a native object, a store),
  which the checker keeps out of spawn, and what's made from them, are
  skipped (`async.in_worker`), so a file's database isn't opened again
  in each worker. The page's core sends a
  job: the closure's function by its index in the module (the VM's and
  lang/cgen's code share it), its captures in `vm/keep.c`'s encoding,
  which names its types; the worker's core runs it and answers its
  result or error the same way, and the page's settles the future
  (`asuka_spawn_done`). A cancelled job that hasn't started never does;
  one that has runs to its end, its answer dropped. A worker that traps
  is replaced; one whose app's init fails fails its jobs with why.
- Each view is an absolutely positioned element at its frame; children
  are placed from the frame's corner, so a border is drawn inside the
  frame over the content (an inset outline), as a layer's is. A measured
  leaf is measured by its own element at the constraints (max-content,
  min-content, or wrapped at the width; a wrapped text as wide as its
  widest line, as UIKit and Android measure), moved into a measuring
  element when it's in a part of the page that isn't displayed.
- Semantic colors are CSS variables with light and dark values; the
  app's accent is the controls' `accent-color`. Text sizes are in `rem`,
  so they follow the browser's text size (`FontScale`).
- Navigation draws its own bars, as the Windows host's: a `Stack`'s
  header (back, title, bar buttons, search), `Tabs`' bottom bar, a
  `SplitView`'s sidebar (a drawer below 700 points). Sheets and alerts
  are modal `<dialog>`s (the top layer, what's under them inert); one
  declared in a `Window` (its `Window` prop) is centered over that
  Window's panel while it shows, and moves with it;
  popovers and menus are `popover` elements. The browser's back goes
  back in the app: a history entry stands for it while there's a dialog
  to close or a stack to pop, and its `popstate` goes back (closing the
  newest dialog, or popping the innermost stack, or what an `onBack` takes).
  With a `Router`, the page's address and history are its (see
  "Routes": history.js), and that entry stands only for what it doesn't
  route.
- `List` is core-virtualized, as on the desktop hosts, in a scrolling
  element: rows placed by the host with transforms, bound through
  `ListBind` as they come into view, recycled as they leave; a row's
  height from its cells' frames, estimated until bound; a chat's list
  kept at its end; swipe actions as the row's context menu.
- `Shader`: the compiler translates the Kage program to GLSL ES 3.00 as
  it compiles the app (internal/shadercode, beside its Metal and AGSL),
  last in the shader's code, since no compiler runs in the page; the
  host compiles it with WebGL 2 and sets the uniforms' lanes by their
  GLSL types.
- Files: the core's file calls (asuka/fs, the crash record, what the C
  library opens) are WASI's, which `wasi.js` answers from a file system
  in memory (`fs.js`, `/` preopened), at once: the app's directory
  (`/data/<name>`, the core's `appDir`) loaded from IndexedDB before the
  core starts and written back as it changes (a large file in chunks of
  64 KiB, only those changed; each data directory's changes in one
  IndexedDB transaction). Tabs share it: a tab writes holding the
  directory's Web Lock, then names the files on a `BroadcastChannel`, and
  the others read them again (one a tab changed and hasn't written stays
  its own, written whole: last writer wins, never a mix of chunks).
  spawn's workers each keep a copy: every change is numbered (a journal),
  a job carries what changed since the worker's last (but its own
  changes), and its answer what the job changed, which the page takes as
  another writer's.
- SQLite: the sqlite plugin's C (SQLite and `sdk/sqlite/csrc/asq.c`), built to
  WebAssembly by `sdk/sqlite/internal/build` (zig, `wasm32-wasi`, `-Os`) with
  `sdk/sqlite/csrc/asq_wasm.c` as its system (`SQLITE_OS_OTHER`, one thread:
  a VFS, the time zone and randomness imported from JavaScript, no WASI
  imports): the plugin's `wasm` file, which the CLI ships with the apps
  that use it, in `lib/` (as any plugin's, [Plugins](#plugins)), named in
  the page's `libs` option; the page starts the worker when it has
  `asqlite.wasm`. It runs in a worker of the
  page's (`platform/web/src/sqlite-worker.js`; `sqlite-engine.js` the
  module, `sqlite-vfs.js` its files, `sqlite-store.js` where they're
  kept). std/sqlite's builtins (`core/src/sqlite/sqlite.c`) send each
  call through `sqlite.js` (`js_sqlite_send`); the worker runs it whole,
  a query's chunks gathered into one rows answer with the tables it read
  and wrote, and the answer comes back to the core (`asuka_sqlite_done`),
  which decodes the rows by their descriptor as on every platform. The
  main thread can't wait for the worker, so what runs on it elsewhere
  (`openConn`, `execNow`, `migrateConn`) is sent without waiting, in
  order before what follows: the page numbers the connection; one that
  didn't open fails each statement with why, and a call that failed
  unseen fails its connection's next one. The data directory's databases are
  files of the origin private file system, read and written in place
  through sync access handles; without them, IndexedDB in 64 KiB chunks,
  written once each call returns. One connection per file, a rollback
  journal. Tabs take turns as wa-sqlite's `OPFSCoopSyncVFS` has them: a
  tab uses a database only holding its Web Lock (its handles open), and
  keeps it until another tab asks on a `BroadcastChannel` and it isn't
  in a transaction; a statement that needs it meanwhile gets
  `SQLITE_BUSY` at once while the lock comes (SQLite never sleeps there),
  and the worker runs it again, outside a transaction, until the busy
  timeout. Between the page's own connections to a file, the VFS locks as
  the unix VFS does between processes. A database the page's files kept
  (before) moves to the worker as it's first opened.
- Crash reports: the core keeps the run's record as on the phones
  (`core/src/std/crash.c`, begun by `core.js` with the browser as the
  device), in the app's files; a page marks its run alive in
  `localStorage` while it's open (`crash.js`: the core asks through
  `crash_alive`; spawn's workers, which read the reports, are told the
  marks and where the page's record is), writes uncaught JavaScript errors
  to the inbox, and on the core's trap keeps its `.crash` file at once.
- Native packages: each package's `web/` module, registered by import
  path (`native.js`), its methods decoding the core's bytes as the
  native hosts do; the host's own modules are in
  `platform/web/src/modules`. What a browser can't do answers an error
  that says so.
- Web views: a page of the app's origin is fetched and written into a
  fresh `<iframe>` (its URL set to its own), with `window.asuka` and
  `Preload` before its scripts, so the host has its script, events,
  `Block` and navigation; another site's is a sandboxed frame that only
  loads, unless it answers Asuka's `postMessage` protocol (docs/web.md).
- Symbols are the Android host's Material Symbols, as SVG paths
  (`platform/web/gen_symbols.go` reads its drawables).
- `asuka dev --platform web` serves the page, the host's files (a checkout's, so
  changing them reloads the page too), the build and the assets, and
  keeps a connection to the page (server-sent events): a new module
  goes to the page, which loads it keeping the app's state and views
  (`asuka_dev_reload`), a failed compile shows its errors over it, and the GUI tests' automation
  commands go through it. `asuka build --platform web` names the files by their
  content's digest, so only `index.html` and the manifest need fetching
  again.

## References

- Solid: owners, `createRoot`, `mapArray` (`For`), `createStore`.
- alien-signals: the propagation algorithm to port.
- Taffy: CSS block, flexbox and grid in Rust; its Chrome-generated
  fixtures define our layout tests.
- The CSS specs: Flexible Box Layout, Grid Layout, CSS 2 visual
  formatting.
- React Native's Fabric: mount items, view flattening, synchronous
  layout; react-navigation's native stack for `Stack`'s behavior.
- Flutter: merging the UI and platform threads for synchronous interop.
- MyGo (`github.com/egoist/mygo`): purego and Objective-C patterns,
  main-thread loop, incremental layout, CLI packaging and signing.
