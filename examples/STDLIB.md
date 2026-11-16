# What the examples use of the standard library

The examples are written in Asuka. This is every API of the standard
library (`import asuka/…`) and of the prelude they call that isn't
declared yet (lang/prelude has the v0 elements and types.asuka), so the
work that declares them knows what to implement. Signatures are Asuka's;
`async` ones are awaited, `throws` ones called with `try`. A type in
`[…]` is an array, `T?` an optional; "store" is a reference with state
(reading it is tracked, writing it notifies), "stream" an async sequence
read with `for await`. Fakes (`Fake…`, `host.…`) are what `asuka test`'s
fake host gives tests. The Go package each module replaces is in
parentheses.

## Prelude

### Functions

| Function | Signature | Used by |
|---|---|---|
| `sleep` | `async fn sleep(d: Duration)` | many |
| `all`, `race` | `async fn all(tasks…) -> (…)`, `async fn race<T>(tasks: T…) -> T`; `all(xs: [Task<T>]) -> [T]` too | services, usage, bluetooth |
| `withTimeout` | `async fn withTimeout<T>(d: Duration, body: async fn() throws -> T) throws -> T` | bookmarks, usage |
| `min`, `max`, `hypot`, `sin`, `cos`, `pi` | numbers | many |
| `print` | `fn print(s: string)` | services |
| `push`, `pop`, `present`, `dismiss`, `back` | navigation, as language.md says | many |
| `navigate`, `replace` | `fn navigate(href: string)`, `fn replace(href: string)`: the router's | navigation |
| `alert` | `fn alert(title: string, message: string = "", buttons: [AlertButton] = [])` | notes, richtext, bookmarks |
| `snackbar` | `fn snackbar(message: string, action: SnackbarAction? = none, duration: SnackbarDuration = .short) -> SnackbarHandle` (`.dismiss()`) | gallery |
| `animate` | `async fn animate(body: fn())`, `async fn animate(a: Animation, body: fn())`: returns when the animation ends | media, gallery, interaction, usage |
| `haptic` | `fn haptic(kind: Haptic)` | navigation, gallery |
| `announce` | `fn announce(text: string)` | gallery |
| `copy`, `share`, `shareFiles` | `fn copy(text: string)`, `fn share(text: string)`, `async fn shareFiles(paths: [string], title: string, from: Ref) throws` | gallery |
| `openURL` | `fn openURL(url: string)` | richtext, usage |
| `quit`, `closeWindow` | `fn quit()`, `fn closeWindow()` | launcher, tray, notes |
| `environment`, `screenEnvironment` | `fn environment() -> Environment` (tracked); `screenEnvironment()`: the root's, the whole screen | navigation, gallery, usage, services |
| `frames` | `fn frames() -> stream Duration`: each frame's time | gallery |
| `webCookies` | `async fn webCookies(url: string) throws -> string` | downloader |
| `devPlayer` | `fn devPlayer() -> bool`: running in asuka dev's player | services |

### Types

`Span(text, font: Font?, italic, underline, strike, code, color: Color?,
highlight: Color?, link: string)`; `AlertButton(title, role:
AlertButtonRole, run: fn()?)`; `SnackbarAction(title, run: fn())`;
`Shortcut(key: string, mods: [Modifier])`, `Shortcut.cmd(key, mods:)`
(Cmd on Apple, Ctrl elsewhere); `Ref` (a view's handle: `ref: r` on an
element, then `r.focus()`, `r.insert(text)`, `r.scrollTo(x, y,
animated:)`, `r.scrollToEnd(animated:)`, `r.scrollToItem(i, animated:)`,
`r.toggleFormat(f)`, `r.setLink(url)`, `r.seek(s)`, `r.play()`,
`r.eval(js) async throws -> string`, `r.goBack()`, `r.reload()`);
`Path` (`arc`, `moveTo`, `lineTo`, `ellipse`, `roundedRect`, `close`:
each returns the path); `Graphics` (a canvas's: `width`, `height`,
`stroke(path, color, width:, cap:)`, `text(s, x, y, font:, align:)`);
`DragItem(kind: ShareKind, mime, text, value)`, `DropTarget(accept:
[string])`, `Drop(items, local)`; `SharedItem(kind, text, path, name,
mime, size, isFile)`; `ShareSheet(items, cancel(), close())`;
`A11yAction(name, run: fn())`; `TextFormats(bold, italic, underline,
strike, mono, link)`; `WebState(url, title, loading, progress,
canGoBack)`; `Gesture(state: GestureState, translationX/Y, velocityX/Y,
scale, rotation)`; `SheetRoute(fullScreen:, detents:, grabber:)`;
`Params(int(name), get(name))`. `Animation` gains `damping`, `velocity`,
`repeat: .forever`, `autoreverses`. `Color.opacity(a)`, `Color.hex()`.
`Action`'s cases take their fields' defaults (`.button(title: "Copy")`),
and its `run` may await.

### Elements, props and events

Beyond the v0 elements (lang/prelude/elements.go):

- Navigation: `Stack`, `screen`'s props (`title`, `largeTitle`,
  `primaryExtended`, `zoomFrom`, `bind search`, `searchPlaceholder`,
  `preventBack`), slots (`toolbar`, `bottomToolbar`, `primary`) and events
  (`backRequest`, `appear`, `disappear`); `Tabs`, `Tab(title) { path,
  icon, badge }`; `Router`, `Route(path) { redirect, screen: fn(Params)
  -> View, sheet: SheetRoute?, guard: fn(string) -> string }`;
  `NavLink(href) { … }`; `SplitView(title)`, `Sidebar(title)`,
  `Destination(title) { icon, badge, toolbar { … } }`; `Sheet { bind open,
  detents, grabber; on dismiss }`; `Alert(title) { bind open, message;
  TextField…; Button… }`; `Popover { bind open, maxWidth; content { … } }`.
- Desktop: `Window { bind open, title, width, height, minWidth, maxWidth,
  minHeight, maxHeight, panel, fitContent, modal, resizable,
  maximizable, alwaysOnTop, stateKey, flyout: Flyout(anchor, placement,
  gap); on closeRequest }`; `WindowCloseGuard { when; on closeRequest }`;
  `WindowKeys { keys }`; `MenuBar { Menu(title) { MenuItem(title) {
  shortcut, role; on press } } }`.
- Controls: `RichText(spans) { on link(url) }`; `DatePicker { bind value:
  time.Time, mode: DateMode, min, inline }`; `ColorPicker { bind value,
  alpha, title }`; `Wheel(items) { bind selected }`; `PageControl(count) {
  bind page }`; `Video(source) { autoPlay, muted, loop; on end }`;
  `WebView(url) { html, preload, block; on state(s), load(url),
  request(url), download(d), message(m) }`; `Shape(path: fn(float, float)
  -> Path) { fill, stroke, lineWidth, lineCap, lineJoin, dash, trimEnd }`;
  `Canvas { draw: fn(Graphics) }`; `Section { header { … } }` in a `List`.
- Props on existing elements: `ref` (any); `keys: [Shortcut: fn()]`,
  `drag: [DragItem]`, `drop: DropTarget` with `on drop(d)`,
  `swipeActions`, `leadingSwipeActions`, `selected` with `on select(on)`
  and `on selectBegin`, `actions: [A11yAction]` (View, Row in a List);
  the state styles `hovered { … }`, `pressed { … }`, `dragging { … }`,
  `dragOver { … }` and the events `hoverChange(on)`, `pressedChange(on)`,
  `pan(g)`, `pinch(g)`, `rotate(g)`, `swipe(direction)` (View, Row);
  `List { gap, bind selecting }`; `Scroll { paging, bind page }`;
  `TextField { bind focused, bind rich: [Span], pasteTypes; accessory { … };
  on paste(items), formats(f) }`; `Image { animated, blurHash, thumbHash }`;
  `Button { role: ActionRole }` in menus and alerts.
- The app's block: `colorScheme`, `shareSheet: fn(ShareSheet) -> View`,
  `pushRewrite: fn(push.Message) throws -> push.Message`, and the events
  `openURL(url)`, `share(items)`, `foreground`, `background`.

### Tests

`mount(view, provide: store, platform: Platform, at: href, setup:
fn(TestHost)) -> TestApp`; `TestApp`: `tap`, `type(text, into:)`,
`submit(label)`, `toggle(label)`, `swipe(text, action:)`, `refresh(text)`,
`scrollTo(text)`, `back`, `dismiss`, `goBack`, `goForward`,
`openURL(url)`, `in(.toolbar)`, `expect(texts…)`, `expectNo(texts…)`,
`texts()`, `disabled(text) -> bool`, `href`, `history() -> ([string],
int)`, `idle()`, `waitFor(text)`, `until(what, cond: fn() -> bool)`,
`host`. `expect(cond, message)` in a test.

## Modules

| Module | API used | Replaces |
|---|---|---|
| `asuka/agerange` | `async fn request(min: int, max: int) throws -> Range` | sdk/agerange |
| `asuka/appearance` | `fn current() -> Appearance` (tracked: `accent: Color?`, `increaseContrast`, `reduceTransparency`) | sdk/appearance |
| `asuka/appleauth` | `SignInButton { style; on press }`; `async fn signIn(scopes: [Scope]) throws -> Credential` (`user`) | sdk/appleauth |
| `asuka/apps` | `App` (`name`, `path`); `async fn list() throws -> [App]`; `fn search(apps: [App], query: string) -> [App]`; `async fn icon(app: App, size: float) throws -> string`; `async fn open(app: App) throws` | sdk/apps |
| `asuka/assets` | `async fn text(path: string) throws -> string` | `//go:embed` |
| `asuka/audio` | `Player(source)`: `play()`, `pause()`, `release()`, `async status() -> Status`; `Status(playing, position, duration, ended)`; `fn play(source: string)` | sdk/audio |
| `asuka/background` | `fn start(title, progress: float, actions: [Action], onAction: fn(string)) -> Task` (`update(title:, text:, progress:, actions:)`, `end()`); `Action(id, title)` | sdk/background |
| `asuka/biometric` | `async fn check() throws -> Status` (`kind`); `async fn authenticate(reason: string) throws` | sdk/biometric |
| `asuka/bluetooth` | `fn state() -> State`; `async fn request() throws -> State`; `fn scan(duplicates: bool) -> stream Advertisement` (throws `BluetoothError`); `async fn choose(filters: [Filter], optionalServices: [UUID]) throws -> Device`; `fn connect(device, timeout:) -> Connection` (store: `state`, `error`; `async services()`, `mtu()`, `characteristic(service, char)`); `Characteristic`: `async read() -> bytes`, `notifications() -> stream bytes`, `properties`; `LocalService`, `LocalCharacteristic(uuid, properties, permissions, value, onRead)` (`notify(bytes)`, `subscribers`); `async fn serve(services) throws`, `async fn advertise(localName:, services:) throws` (run until cancelled); UUID constants `heartRate`, `battery`, `batteryLevel`, `heartRateMeasurement`, `bodySensorLocation`, `deviceInformation`; fakes `FakePeripheral`, `FakeService`, `FakeCharacteristic`, `host.bluetooth` | sdk/bluetooth |
| `asuka/calendar` | `async fn request() throws -> Access`; `async fn calendars() throws -> [Calendar]`; `async fn events(from:, to:) throws -> [Event]`; `async fn add(e: Event) throws -> bool` | sdk/calendar |
| `asuka/camera` | `camera.View { mode, scan; on code(c), error(m) }`; `async fn capture() throws -> string`; `async fn take() throws -> string?` | sdk/camera |
| `asuka/chart` | `chart.View(series: [Series]) { labels, label }`; `Series(name, kind: .bar \| .line \| .area, values, smooth, color)` | sdk/chart |
| `asuka/compose` | `async fn mail(to:, subject:, body:, html:, attachments: [Attachment]) throws -> Result`; `async fn message(to:, body:) throws -> Result` | sdk/compose |
| `asuka/contacts` | `async fn pick() throws -> Contact?` | sdk/contacts |
| `asuka/crash` | `fn reports() -> stream Report`; `fn breadcrumb(s)`, `fn set(key, value)`, `fn record(e: Error)`, `fn delete(id)` | sdk/crash |
| `asuka/crypto` | `fn sha256(data: bytes) -> bytes`; `fn randomBytes(n: int) -> bytes` | crypto/* |
| `asuka/device` | `async fn info() throws -> Info` (`model`, `system`, `systemVersion`); `async fn browse(url) throws`; `async fn pickPhotos(max: int) throws -> [string]`; `async fn pickFiles(multiple: bool) throws -> [File]`; `async fn clipboardText() throws -> string?`; `clipboardHas()`, `clipboardImage()`, `copyImage(path)` | sdk/device |
| `asuka/dialog` | `async fn chooseFolder(button: string) throws -> string?`; `async fn save(name:, button:, filters: [Filter]) throws -> string?` | sdk/dialog |
| `asuka/dock` | `fn setProgress(p: Progress)` (`value`, `state`); `fn requestAttention(kind)` | sdk/dock |
| `asuka/document` | `Documents<T>(types: [Type], read: fn(bytes) throws -> T, write: fn(T) -> bytes) { width, height, editor: fn(Document<T>) -> View }`; `Document<T>` (store: `content`, `state`; `async revert()`, `async saveAnyway()`); `Type(name, extensions, mimeTypes)` | sdk/document |
| `asuka/encoding` | `fn base64Encode(b: bytes) -> string`, `fn base64Decode(s) throws -> bytes`, `fn base64URLEncode(b: bytes) -> string` | encoding/base64 |
| `asuka/fs` | `fn appDir() -> string`; `join(…)`, `dir(p)`, `ext(p)`; blocking (call in `spawn`): `readDir(p) throws -> [Entry]`, `readText(p) throws`, `write(p, data) throws`, `copy(from, to) throws`, `mkdirAll(p) throws`, `remove(p) throws`, `removeAll(p) throws`; `create(p) throws -> File` (`async write(bytes) throws`, `close()`) | os, path/filepath |
| `asuka/hotkey` | `Hotkey(shortcut) { description; on press; on error(e: HotkeyError) }` | sdk/hotkey |
| `asuka/html` | `fn unescape(s) -> string` | html |
| `asuka/http` | `async fn get(url, headers: [string: string] = [:]) throws -> Response`; `async fn post(url, headers:, body: bytes) throws -> Response`; `Response`: `status`, `statusText`, `url`, `contentLength`, `body: stream bytes`, `async text(limit: int = 0) throws`; `async fn loopback(path:, page:) throws -> Loopback` (`url`, `async next() throws -> string`, `close()`); `HttpError.status(int, string)` | net/http |
| `asuka/i18n` | `fn formatNumber(n) -> string` (tracked: the format locale); `fn languages() -> [string]`; `fn locale() -> string`; `fn setLocale(tag)`; `fn autonym(tag) -> string` | i18n, ui |
| `asuka/icloud` | `fn account() -> Account` (`available`); `fn files(scope) -> [File]` (tracked); `Value<T>(key, default)` (a store: `value`); `async documents()`, `readFile(p) -> bytes`, `writeFile(p, data)`, `conflicts(p) -> [Version]`, `resolve(p, keeping: Version?)`; fake `host.icloud` | sdk/icloud |
| `asuka/imageedit` | `async fn edit(path, actions: [Action], quality:) throws -> Result`; `async fn videoFrame(path, at: Duration, format:) throws -> Result` | sdk/imageedit |
| `asuka/integrity` | `fn supported() -> bool`; `async fn generateKey() throws -> string` | sdk/integrity |
| `asuka/js` | `async fn eval(script: string) throws -> string` | sdk/js |
| `asuka/json` | `fn decode<T>(text) throws -> T` (T from the expected type); `fn encode<T>(v: T) -> string`; `Value` (dynamic: `[key]`, `string()`) | encoding/json |
| `asuka/library` | `async fn save(path, collection:, folder:, move:) throws -> string` | sdk/library |
| `asuka/liveactivity` | `async fn start(c: Content) throws -> Activity` (`update(c, alert:)`, `end(c, dismiss:)`); `fn events() -> stream Event` | sdk/liveactivity |
| `asuka/location` | `fn authorization() -> Authorization` (tracked); `async request()`, `requestAlways()`, `requestPrecise(purpose)`, `current() -> Position`, `updating() -> bool`, `startUpdates(…)`, `monitor(Region)`, `stopMonitoringAll()`, `openSettings()`; `stopUpdates()`, `supported()`; `fn updates() -> stream Batch`, `fn crossings() -> stream Crossing` (`defer() -> fn()`); fake `host.location` | sdk/location |
| `asuka/lottie` | `lottie.View(source) { bind playing, loop, speed, bind progress, marker, overrides: [Override]; on complete }`; `Override.colors(layer, light:, dark:)` | sdk/lottie |
| `asuka/maps` | `maps.View { bind region, markers; on markerPress(id), press(c), error(m) }`; `Region`, `Coordinate`, `Marker` | sdk/maps |
| `asuka/markdown` | `markdown.View(text) { streaming }` | sdk/markdown |
| `asuka/network` | `fn status() -> Status` (tracked: `connected`, `metered`, `type`) | sdk/network |
| `asuka/nfc` | `fn state() -> State`; `async fn start(alert:) throws -> Session` (`async next() throws -> Tag`, `setAlert`, `fail`, `stop`); `fn opened() -> stream Tag`; `Tag` (`id`, `techs`, `message`, `ndef`, `async write(Message)`); `Message(records)`, `Record.uri`, `Record.text`, `decode(bytes)`, `NfcError`; fakes | sdk/nfc |
| `asuka/notify` | `async request() -> bool`, `show(n)`, `schedule(n)`, `showing() -> [Delivered]`; `setCategories`, `setChannels`, `setChannelGroups`, `present(fn(Delivered) -> Presentation)`, `responses() -> stream Response`, `removeThread`, `removeAll`, `setBadge`; `Notification(…).from(person, conversation)` | sdk/notify |
| `asuka/nowplaying` | `NowPlaying { title, artist, album, artwork, player, skipInterval; on next, previous }` (no view) | sdk/nowplaying |
| `asuka/pay` | `async fn available(r: Request) throws -> Availability`; `async fn pay(r) throws -> Payment`; `async fn setUp() throws`; `PayButton { type, style; on press }`; `Request(…, onShippingContact, onShippingMethod, onCoupon, onAuthorize)`; `Invalid(field, message, update)`; `PayError.canceled`; fakes | sdk/pay |
| `asuka/periodic` | `fn runs(id) -> stream Run`; `fn schedule(id, every:, network:)` | sdk/periodic |
| `asuka/power` | `fn status() -> Status` (tracked); `fn locks()`, `fn suspends()` (streams); `async fn idleTime() throws -> Duration` | sdk/power |
| `asuka/preview` | `async fn show(paths, from: Ref) throws` | sdk/preview |
| `asuka/print` | `async fn html(markup, job:) throws -> Result`; `async fn pdf(markup) throws -> string`; `async fn webView(r: Ref) throws` | sdk/print |
| `asuka/purchase` | `Item(id, kind)`; `fn declare([Item])`; `async products([Item]) -> [Product]`, `buy(Item) -> Result`, `restore()`, `manageSubscriptions(id)`; `fn owned() -> Owned` (tracked: `has(id)`); `fn events() -> stream Event`; `Transaction.finish()`; fakes | sdk/purchase |
| `asuka/push` | `async fn register() throws -> Registration`; `fn opened() -> stream Message`; `Message` (`data`, `from(person, chat)`) | sdk/push |
| `asuka/random` | `fn float() -> float` | math/rand |
| `asuka/record` | `async fn start(maxDuration:) throws -> string`, `fn stop()` (video); `async fn audio(maxDuration:) throws -> string`, `fn stopAudio()` | sdk/camera/record, sdk/audio/record |
| `asuka/regex` | `Regex(pattern)`: `find(s) -> string?`, `findAll(s) -> [string]`, `findSubmatch(s) -> [string]?`, `findAllSubmatches(s) -> [[string]]`, `matches(s) -> bool` | regexp |
| `asuka/review` | `async fn request() throws` | sdk/review |
| `asuka/scan` | `async fn file(path) throws -> [Code]` | sdk/scan |
| `asuka/screen` | `fn displays() -> [Display]` (tracked); `lock(orientation)`, `setBars(hideStatus:)`, `preventCapture(on)` | sdk/screen |
| `asuka/secure` | `async save(key, value) throws`, `load(key) throws -> string?`, `remove(key) throws` | sdk/secure |
| `asuka/sensors` | `fn available(s: Sensor) -> bool`; `motion(every:)`, `accelerometer(every:)` (streams); `async steps(from:, to:) throws`; `async simulate(json) throws` | sdk/sensors |
| `asuka/shader` | `Program(source)`; `Shader(program) { uniforms: [Uniform], animated }` | sdk/shader |
| `asuka/shell` | `async fn reveal(path) throws` | sdk/shell |
| `asuka/speech` | `async fn speak(text) throws` (ends when spoken, or `.stopped`); `pause`, `resume`, `stop`, `speaking() -> bool`; `async voices() throws` | sdk/speech |
| `asuka/splash` | `fn hold()`, `fn hide()` | sdk/splash |
| `asuka/sqlite` | `fn open(path) throws -> DB`; `DB`: `migrate([string]) throws`, `async exec<A>(sql, args: A) throws` (A: a tuple or an array, encoded by its type), `async query<T, A>(sql, args: A) throws -> [T]`, `watch<T, A>(sql, args: A) -> Live<T>` (tracked: `rows`, `loading`; runs again as its arguments or its tables change), `changed()` | sdk/sqlite |
| `asuka/storage` | `Value<T>(key, default)` (a store, kept on the device: `value`); `fn flush()`; `fn dirName() -> string` | sdk/store |
| `asuka/svg` | `Icon(source)`; `svg.View(icon) { color }` | sdk/svg |
| `asuka/time` | `Time` (`format(pattern)`, `sameDay(t)`, `truncate(d)`, `startOfDay()`, `year`, `day`, `hour`, `unix()`, `unixNano()`; `+`/`-` a Duration, `-` a Time); `now()`, `date(y, m, d, hour:, minute:, zone:)`, `parse(rfc3339) throws`; `Duration.seconds`, `.hours`, `.rounded(d)` | time |
| `asuka/tracking` | `async fn request() throws -> Status` | sdk/tracking |
| `asuka/tray` | `Tray(id) { symbol, title, tooltip, menu: [Action]; on click }`; `DockMenu { items }`; `fn anchor(id) -> Anchor` | sdk/tray |
| `asuka/url` | `fn parse(s) -> URL?` (`host`, `query(name) -> string?`, `resolve(ref) -> string`); `fn escape(s)`; `fn encodeQuery([string: string])` | net/url |
| `asuka/watch` | `async fn path(dir, ignore: [string]) throws -> Watch` (`events: stream [Event]`); `Event(op, path, oldPath)` | sdk/watch |
| `asuka/webauth` | `async fn open(url) throws -> string` (the redirect; `WebAuthError.canceled`) | sdk/webauth |
| `asuka/widget` | `fn set(kind, e: Entry)` | sdk/widget |

Strings, arrays and numbers take the methods the examples call on them:
`trim`, `isEmpty`, `count`, `slice(from, to)`, `find`, `findAny`,
`hasPrefix`, `hasSuffix`, `trimPrefix`, `trimSuffix`, `replace(old,
new, count:)`, `split(sep, count:)`, `fields()`, `lower`, `upper`,
`contains`, `bytes()`, `int()`, `characters`; `push`, `insert(at:)`,
`remove(at:)`, `removeWhere`, `index(where)`, `first(where)`, `first`,
`last`, `filter`, `map`, `count(where)`, `sorted`, `sortStable`,
`reversed`, `prefix`, `suffix`, `join`, `enumerated`, `+`; `bytes(…)`,
`bytes.hex(sep)`, `bytes.string()`; `int.padded(width)`, `int.float()`,
`int.abs()`, `float.int()`, `float.fixed(digits)`, `float.rounded()`,
`float.abs()`.
