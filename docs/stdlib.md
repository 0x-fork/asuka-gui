# The standard library

The standard library is the `asuka/...` packages. They come with the
`asuka` CLI, so they need no `asuka add`: import one and use it.

```asuka
import asuka/http
import asuka/store

state units = store.read("units", "metric")
```

Most need no platform code: they're Asuka, and what Asuka can't say is
the runtime's own C, so they work the same everywhere (`asuka/json` and
`asuka/math` are part of the compiler). The others are
[native packages](native.md) like any other: each platform runs them
with its own APIs (URLSession on iOS and macOS, Android's own classes,
the browser's on the web, Go on Windows and Linux), so they behave as
they do for the platform's own apps.

| Package | What it has |
|---|---|
| `asuka/integrity` | `integrity.supported()`, `generateKey()`, `attestKey(id, challenge)`, `assertion(id, data)` (App Attest on iOS and the Mac); `prepare(cloudProjectNumber:)`, `token(requestHash:)` (Play Integrity) |
| `asuka/js` | `js.eval(script, timeout:)`: JavaScript run off screen in the platform's own engine (WKWebView, Android's WebView, WebView2, WebKitGTK, a sandboxed frame), its value as JSON; `js.JsError.timeout` |
| `asuka/json` | `json.decode<T>(text)` and `json.encode(value, indent:)`: typed JSON, by the compiler's knowledge of `T` |
| `asuka/math` | `sqrt`, `pow`, `floor`, `ceil`, `round`, `sin`, `cos`, `atan2`, `clamp`, `random`, `randomInt`, `seed`, `pi` |
| `asuka/time` | `time.now()`, `time.date(2026, 3, 7, hour: 9, zone: .utc)`, `time.unix(s)`, `time.parse(rfc3339)`: a `Time`, an instant with its zone (`.local`, `.utc`, `.fixed(seconds:)`) and its wall clock's `year`, `month`, `day`, `hour`, `minute`, `second`, `nanosecond`, `weekday`. Its methods: `format("EEE d MMM, HH:mm")` (Unicode's date patterns, in English), `rfc3339()`, `add(d)`, `sub(t)` (a `Duration`), `addDate(years:, months:, days:)`, `isBefore`, `isAfter`, `compare`, `sameDay`, `startOfDay`, `truncate(d)`, `inZone`, `utc`, `local`, `unix`, `unixMilli`, `unixNano`. `time.monotonic()` (a `Duration` that only goes forward), `time.since(t)`, `time.ticks(1s)` and `time.ticker(1s)` (a stream of times) |
| `asuka/url` | `url.parse(text)`: a `URL?` of `scheme`, `user`, `host`, `port`, `path`, `rawQuery`, `fragment`, with `query(name)`, `queryAll(name)`, `text()` and `resolve(ref)` (RFC 3986); `url.resolve(base, ref)`, `escape`, `unescape`, `pathEscape`, `pathUnescape`, `encodeQuery(map)` (names sorted), `parseQuery(raw)` |
| `asuka/regex` | `regex.compile(pattern)` (it throws a bad pattern): a `Regex` with `matches`, `find`, `findAll`, `findRange`, `findSubmatch`, `findAllSubmatches`, `replace(text, with: "$1")`, `replaceEach(text, f:)`, `split`, `groupNames`; `regex.quote(text)`. RE2's syntax, matched in linear time (no backtracking) |
| `asuka/encoding` | `base64Encode`, `base64URLEncode` (unpadded), `base64Decode` (either alphabet), `hexEncode`, `hexDecode` |
| `asuka/crypto` | `sha256`, `sha512`, `sha1`, `hmacSHA256(key:, data:)`, `randomBytes(n)` (the system's randomness), `uuid()` (version 4), `equal(a, b)` (in constant time); Ed25519 (RFC 8032): `ed25519Verify(publicKey, message, signature)`, `ed25519Sign(privateKey, message)`, `ed25519PublicKey(privateKey)`, `ed25519NewKey()` |
| `asuka/html` | `escape(text)` and `unescape(text)` (numeric references and the named ones text uses) |
| `asuka/random` | `random.int(1, 6)` (both ends included), `float()`, `chance(p)`, `pick(items)`, `shuffled(items)`, `seed(n)` |
| `asuka/fs` | `fs.appDir()` (the app's own directory), `join([…])`, `clean`, `dir`, `base`, `ext`; and calls that block, for `spawn { }`: `readDir` (an `Entry`: `name`, `isDir`, `size`, `modified`), `stat`, `read`, `readText`, `write`, `writeText`, `append`, `appendText`, `copy`, `rename`, `mkdirAll`, `remove`, `removeAll`, `exists`. A failure throws an `FsError` (`.notFound(path)`, `.exists`, `.denied`, `.notDirectory`, `.isDirectory`, `.notEmpty`, `.failed(path, reason)`) |
| `asuka/apps` | `apps.list()`, `apps.search(apps, query)` (a launcher's ranking: the name, its start, a word's start or its words' initials, anywhere, its letters in order; case and accents aside), `apps.icon(app, size)` (a PNG's path), `apps.open(app)`: the installed apps on macOS, Windows and Linux; `AppsError.unsupported` on a phone and the web |
| `asuka/dock` | `dock.setProgress(Progress(value:, state:))` and `dock.requestAttention(.critical)`: the app's icon on macOS's Dock, Windows' taskbar and Linux's launcher (Unity's launcher API, where the desktop has it); nothing on a phone or the web |
| `asuka/hotkey` | `hotkey.Hotkey(hotkey.Shortcut(key:, primary:, shift:, alt:, control:)) { description; on press; on error(e) }`: a global shortcut while it's shown (RegisterEventHotKey, RegisterHotKey, the GlobalShortcuts portal); `.notAvailable` on a phone and the web |
| `asuka/tray` | `tray.Tray(id) { image, symbol, template, title, tooltip, menu: [Action]; on click }`: an icon in the tray while it's shown (the Mac's menu bar, Windows' notification area, a StatusNotifierItem); `tray.DockMenu { items }`: the Dock icon's menu, a jump list's tasks, a launcher's quicklist; `tray.anchor(id)`: a flyout `Window`'s anchor; `async supported()`. Nothing on a phone and the web |
| `asuka/i18n` | the language the app shows: `i18n.locale()`, `setLocale(tag)`, `languages()`, `preferredLanguages()`, `isRTL()`, `autonym(tag)`, `region()`; numbers and dates its way: `formatNumber`, `formatInt`, `formatPercent`, `formatDate(t, date:, time:)`. The app's messages are `tr.key(…)` (see [Languages](i18n.md)) |
| `asuka/agerange` | `agerange.request(13, 18)`: the user's age range without their birth date (Declared Age Range on iOS 26, Play's Age Signals): a `Range` (`status`, `lower`, `upper`, `source`); `AgeRangeError.notAvailable` elsewhere |
| `asuka/appearance` | `appearance.current()` (tracked: `accent` as a `Color?`, `increaseContrast`, `reduceTransparency`; it watches the settings only while something reads it), `appearance.now()`, `appearance.changes()` (a stream). The accent is the Mac's, Windows's, the settings portal's on Linux, Material You's on Android, CSS's AccentColor; iOS has none |
| `asuka/biometric` | `biometric.check()` (`kind`, `enrolled`, `passcode`), `biometric.authenticate(reason:, cancel:, noPasscode:)`: LocalAuthentication, BiometricPrompt, Windows Hello, WebAuthn's platform authenticator; `BiometricError.canceled`, `.notAvailable`, `.failed` |
| `asuka/device` | `device.info()` (`model`, `system`, `systemVersion`, `appVersion`, `build`), `device.browse(url)` (in the app: SFSafariViewController, a Custom Tab), `device.pickPhotos(max:)`, `device.pickFiles(types:, multiple:)` (the system's pickers, no permission; copies in the app's caches), `device.clipboardText()`, `clipboardHas()`, `clipboardImage()`, `copyImage(path)`, `clipboardChanges()` (a stream), `device.keepAwake(on)` |
| `asuka/dialog` | `dialog.open(...)`, `dialog.save(name:, ...)`, `dialog.chooseFolder(...)`, `chooseFolders`: the system's file dialogs (NSOpenPanel and NSSavePanel, the common item dialogs, GtkFileDialog, the File System Access pickers), the user's own paths; `dialog.Filter(name:, types:, extensions:)`. Phones: open is the document picker |
| `asuka/http` | `http.get`, `http.post`, `http.postJSON`, `http.request`: requests through the platform's own client, so proxies, trusted certificates, caches and cookies work as the system's do. A `Response` has `status`, `url`, `headers`, `body`, `text()`, `json<T>()` and `ok()` |
| `asuka/login` | `login.set(on)` (`LoginError.needsApproval` on macOS until the user allows it), `login.enabled()`, `login.openedAtLogin()`: start the app as the user logs in (SMAppService, the Run key, XDG autostart); a phone's app can't |
| `asuka/network` | `network.status()` (tracked: `connected`, `type`, `metered`, `constrained`; it watches the network only while something reads it), `network.current()`, `network.changes()` (a stream). NWPathMonitor on iOS and macOS, ConnectivityManager on Android, the Network List Manager on Windows, GNetworkMonitor and NetworkManager on Linux, `navigator.onLine` and the Network Information API on the web |
| `asuka/platform` | `platform.call(module, method, args)` and `platform.send`: the hosts' own modules, by name, with arguments in their little-endian encodings (`u8`, `u32`, `f64`, `str`, `readU32`, ...). For the standard library's packages; an app reaches its own platform code through [native packages](native.md) |
| `asuka/recent` | `recent.add(path)`, `recent.clear()`, `recent.list()`: the documents the user opened lately, in Open Recent and the Dock menu, the jump list, the recently used files |
| `asuka/store` | `store.read(key, default)`, `store.write(key, value)`, `store.remove(key)`, `store.flush()`, `store.dir()`: small values kept across launches as JSON, in the platform's preferences (UserDefaults, SharedPreferences, `localStorage`, a file on Windows and Linux, written a moment after a change) |
| `asuka/notify` | Local notifications: `request()`, `show(n)`, `schedule(n)` (after `n.after`), `cancel(id)`, `showing()`, `remove(ids)`, `removeThread(t)`, `removeAll()`, `setBadge(n)`; `setCategories` (actions, replies), `setChannels`, `setChannelGroups` (Android); `Notification(…).from(person, conversation)` (messages); `present(decide)` (how one shows while the app is in front); `responses()` (a stream of taps, actions and replies; `r.defer()` keeps the app running for the work). The host's notifications on each platform: UserNotifications, Android's, Windows' toasts, Linux's, the browser's |
| `asuka/push` | `push.register()` (the token: APNs', FCM's, the web's subscription), `push.opened()` and `push.received()` (streams of `Message`s; `Message.from(person, conversation)`) |
| `asuka/watch` | `watch.path(dir, ignore:, recursive:, attributes:)` gives a `Watch`: its `root`, and `events`, a stream of batches of `Event`s (`op`: create, write, remove, rename, attrib, overflow; `path`, `oldPath`). FSEvents, inotify and ReadDirectoryChangesW (internal/fswatch); phones and the web: unsupported |
| `asuka/background` | `background.start(title:, text:, progress:, actions:, onAction:)` gives a `Task` (`update(…)`, `end()`): long work shown by the system (Android's foreground service notification, iOS's continued processing, the desktops' progress) |
| `asuka/calendar` | `calendar.request()` (`Access`), `calendars()`, `events(from:, to:, calendars:)`, `add(Event)` (the system's form): EventKit, the Calendar Provider |
| `asuka/compose` | `compose.mail(to:, cc:, bcc:, subject:, body:, html:, attachments:)` and `compose.message(to:, body:, …)` show the system's composers and answer a `Result` (cancelled, sent, saved, unknown); `check()` |
| `asuka/contacts` | `contacts.pick()` (the system's picker: a `Contact` or none), `request()`, `list(name:, limit:)`, `add(Contact)` |
| `asuka/library` | `library.save(path, collection:, name:, folder:, mime:, move:)`: a file into Photos, the gallery, Downloads or Music; where it went |
| `asuka/liveactivity` | `liveactivity.start(Content)` gives an `Activity` (`update(c, alert:)`, `end(c, dismiss:)`), `events()` (started, ended, push tokens), `enabled()`: iOS's Live Activities, Android 16's Live Updates |
| `asuka/periodic` | `periodic.schedule(name, every:, network:, charging:)`, `cancel(name)`, `runs(name)` (a stream of `Run`s, `run.finish(ok)`): iOS's background tasks, WorkManager |
| `asuka/photos` | The photo library with permission: `request()`, `current()`, `list(screenshots:, offset:, limit:)`, `thumbnail(id, size:)`, `export(id)`, `manageLimited()` |
| `asuka/widget` | `widget.set(kind, Entry)`, `setAll(kind, entries)`, `clear(kind)`: what the app's widgets show |
| `asuka/crash` | `crash.reports()` (a stream: the reports kept, then each as it comes; `Report`: `id`, `kind`, `fatal`, `time`, `message`, `text`, `threads`, `context` with breadcrumbs and values), `breadcrumb(message)`, `set(key, value)`, `record(e)`, `delete(id)`. The record is the desktops' hosts' (sdk/crash) and the core's on iOS, Android and the web (its own fatal errors, uncaught exceptions, iOS's MetricKit diagnostics, how Android's runs ended, uncaught JavaScript errors) |
| `asuka/icloud` | iCloud on iOS and macOS: `account()` and `files(scope)` (tracked: as iCloud changes them), `Value(key, default)` (a store kept in iCloud's key-value store), `documents()`, `container(id)`, `readFile`, `writeFile`, `delete`, `rename`, `download`, `evict`, `conflicts(path)`, `resolve(path, keeping:)`. Elsewhere: no account, empty lists, "icloud: not available" |
| `asuka/document` | `document.open(path, read:, write:)` gives a `Document<T>` store: `content`, `state` (loading, saved, edited, saving, conflict, failed), `error`; `edit(c)` (autosaved a moment later), `save()`, `saveAnyway()`, `revert()`. Files coordinated on iOS and macOS. The windows, menus and document browser of a document-based app (`Documents<T>`) wait for the UI runtime's windows and navigation |
| `asuka/host` | What the packages above build on: a host's own modules (`host.call(module, method, args)`, `host.next(module, method)`); apps don't need it |
| `asuka/sqlite` | `sqlite.open(name)` (a file in the app's data directory, an absolute path, or `":memory:"`) gives a `DB`: `migrate(steps)`, `exec(sql, args)` and `query<T>(sql, args)` (async, off the main thread; rows decoded straight into `T`, a struct's fields by the columns' names), `watch<T>(sql, args)` (a `Live<T>` store: `rows`, `loading`, `error`; run again as its arguments change and as a table it reads is written), `changed()`, `close()`. SQLite is the sqlite plugin's library (sdk/sqlite), shipped with the app; on the web, in a worker of the page's |
| `asuka/storage` | `storage.Value(key, default)`: a store whose `value` is kept on the device (asuka/store's), tracked and written as it changes; `reset()`; `storage.flush()`, `storage.dirName()` |
| `asuka/power` | `power.status()` (tracked: `battery`, `onBattery`, `level`, `charging`, `lowPower`, `thermal`), `power.events()`, `suspends()`, `resumes()`, `locks()`, `unlocks()` (streams), `power.idleTime()`: IOKit and ProcessInfo, UIDevice, BatteryManager and PowerManager, GetSystemPowerStatus and the session's notifications, UPower and logind, the Battery Status API |
| `asuka/preview` | `preview.show(sources, start:, edit:)`: files (paths, assets, URLs) in the system's viewer (Quick Look on iOS and the Mac; the file's app elsewhere; a new tab on the web); `preview.PreviewError.noViewer` |
| `asuka/print` | `print.file(source, job:, orientation:)`, `print.html(markup, ...)`: the system's print dialog, a `print.Outcome`; `print.pdf(markup, page:, landscape:, margins:)`: a PDF of markup, its path; `print.Page.a4`, `letter`..., `print.regionPage(locale)`. Its name is the prelude's `print` too: `import asuka/print as printing` |
| `asuka/review` | `review.request()`: the store's rating prompt (StoreKit on iOS and the Mac, Play's in-app review), shown or not on the store's quota; nothing on Windows, Linux and the web. `review.appStoreURL(id)`, `review.playStoreURL(id)` for a "Rate us" button |
| `asuka/screen` | `screen.displays()` (tracked: `id`, `name`, `bounds`, `workArea`, `scale`, `refreshRate`, `primary`), `screen.primary`, `nearest`, `matching`; `screen.lock(orientation)`, `screen.setBars(style:, hideStatus:, hideNavigation:)`, `screen.preventCapture(on)`, `screen.screenshots()` (a stream) |
| `asuka/secure` | `secure.read(key)`, `secure.write(key, value)`, `secure.remove(key)`: small secrets encrypted by the platform (the Keychain, the Android Keystore, Credential Manager, the user's keyring through libsecret, IndexedDB under a non-extractable WebCrypto key) |
| `asuka/shell` | `shell.reveal(path)`, `shell.open(path)`, `shell.trash(path)`, `shell.beep()`: the file manager's (NSWorkspace, Explorer and the Recycle Bin, GIO and FileManager1); `shell.ShellError.noApp` |
| `asuka/splash` | `splash.hold()`, `splash.hide()`: keeps asuka.toml's launch screen up while the app gets ready (iOS, Android; nothing elsewhere) |
| `asuka/tracking` | `tracking.request()`, `tracking.current()`: App Tracking Transparency's prompt on iOS and the Mac (`.notAsked`, `.restricted`, `.denied`, `.allowed`); Windows' advertising ID setting; the browser's Global Privacy Control or Do Not Track; `.allowed` on Android and Linux, which have no prompt |
| `asuka/webauth` | `webauth.open(url, callbackScheme:, ephemeral:)`: a sign-in in the system's sheet (ASWebAuthenticationSession, a Custom Tab; WebView2 and WebKitGTK windows; a popup), the callback URL; `WebAuthError.canceled`; cancelling the task closes it |
| `asuka/location` | `location.authorization()` (tracked), `request()`, `requestAlways()`, `requestPrecise(purpose)`, `current()`, `positions()` (a stream), `placeName(lat, lon)`, `openSettings()`, `supported()`; background updates (`startUpdates(…)`, `updates()`, `stopUpdates()`, `updating()`) and regions (`monitor(region)`, `crossings()`, `stopMonitoring(id)`, `monitored()`), their events kept until read and held while the app works (`hold()`). Core Location, Android's location manager, WinRT's Geolocator, the location portal or GeoClue, the Geolocation API. [Device and services](device.md#location-asukalocation) |
| `asuka/sensors` | `sensors.available(sensor)`; `accelerometer(every:)`, `gyroscope`, `magnetometer`, `motion` (attitude, gravity, user acceleration), `pressure`, `light`, `stepUpdates()` (streams); `steps(from:, to:)`; `simulate(json)`. Core Motion, SensorManager, the Generic Sensor API, iio-sensor-proxy. [Device and services](device.md#motion-and-sensors-asukasensors) |
| `asuka/purchase` | In-app purchases: `declare(items)`, `products(items)`, `buy(item, options)`, `Transaction.finish()`, `events()` (a stream from the app's start), `owned()` (tracked: `has(id)`), `entitlements()`, `restore()`, `statuses(group)`, `manageSubscriptions(id)`, `redeemCode()`, `requestRefund(t)`, `storefront()`. StoreKit 2, Play Billing, the Microsoft Store, the Digital Goods API. [In-app purchases](purchases.md) |
| `asuka/pay` | Apple Pay and Google Pay: `available(request)`, `pay(request)` with async handlers for the shipping contact, method, card, coupon and authorization (an `Update`, or `throw Invalid(field:, message:)`), `setUp()`, the wallet's `PayButton`. PassKit, play-services-wallet, the Payment Request API and Google's pay.js |
| `asuka/appleauth` | Sign in with Apple: `signIn(scopes:, nonce:, state:)`, `credentialState(user)`, `available()`, Apple's `SignInButton`. iOS and macOS |
| `asuka/nfc` | NFC tags: `state()` (tracked), `start(alert:, poll:)` for a `Session` (`next()`, `setAlert`, `fail`, `stop`), `Tag`s (`message`, `ndef`, `write`, `makeReadOnly`, `transceive`), `opened()`, NDEF `Message`s and `Record`s (`encode()`, `decode(bytes)`). Core NFC, Android's reader mode, Web NFC. [NFC tags](nfc.md) |
| `asuka/bluetooth` | Bluetooth LE: `state()` (tracked), `request()`, `scan()` (a stream), `choose(filters)`, `connect(device)` (a `Connection` store: `state`, `services()`, `characteristic(s, c)`), characteristics' `read`, `write`, `notifications()`; `serve(services)`, `advertise(…)`; the SIG's UUIDs. Core Bluetooth, Android's BLE, WinRT's, BlueZ, Web Bluetooth. [Bluetooth](bluetooth.md) |
| `asuka/audio` | `audio.play(source)`, and `audio.Player(source)`, a store with `play`, `pause`, `seek`, `setVolume`, `setRate`, `setLoop`, `status()` and `release()`. AVPlayer, Media3's ExoPlayer, `<audio>`, MCI, GStreamer ([Sound](device.md#sound-asukaaudio)) |
| `asuka/nowplaying` | `nowplaying.NowPlaying`: what the app plays, on the lock screen and the system's media controls, and the commands they send back (the media keys, the headphones'), following an `audio.Player` if given one ([Now Playing](device.md#now-playing-and-the-media-keys-asukanowplaying)) |
| `asuka/speech` | `speech.speak(text, options:)`, `stop`, `pause`, `resume`, `speaking`, `voices()`: text to speech, in the system's voices ([Text to speech](device.md#text-to-speech-asukaspeech)) |
| `asuka/record` | `record.audio(maxDuration:, quality:, background:)` and `stopAudio`, `pauseAudio`, `resumeAudio`, `audioStatus()`: sound from the microphone; `record.start()` and `stop()`: video from the `camera.View` on screen ([Recording sound](device.md#recording-sound-asukarecord)) |
| `asuka/camera` | `camera.View` (`facing`, `mode`, `scan`, `codes`; `code`, `error`): the camera, live; `camera.capture()`, `camera.take()` (the system's camera screen), `camera.record()` ([The camera](device.md#the-camera-asukacamera)) |
| `asuka/scan` | `scan.file(path, kinds:)`: the barcodes and QR codes in an image ([Barcodes](device.md#barcodes-and-qr-codes-asukascan)) |
| `asuka/vision` | `vision.analyze(path, text:, faces:, codes:, languages:)`: an image's text, line by line and word by word, faces and codes, each where it is, on the device ([What's in an image](device.md#whats-in-an-image-asukavision)) |
| `asuka/transcribe` | `transcribe.check(language)`, `transcribe.file(path, language:)`: a recording as text, on the device only (`notAvailable` where it can't be: the web, Linux, older Android) ([Speech to text](device.md#speech-to-text-asukatranscribe)) |
| `asuka/imageedit` | `imageedit.edit(source, actions, format:, quality:)` (resize, crop, rotate, flip, fill, pixelate), `imageedit.size`, `imageedit.videoFrame`: images and video frames, written as JPEG or PNG ([Editing images](device.md#editing-images-and-video-frames-asukaimageedit)) |
| `asuka/maps` | `maps.View` (`bind region`, `markers`, `mapType`, `showsUser`, `interactive`; `markerPress`, `press`, `regionChange`): Apple Maps, Google Maps, OpenStreetMap ([Maps](device.md#maps-asukamaps)) |
| `asuka/lottie` | `lottie.View(source)` (`bind playing`, `bind progress`, `loop`, `speed`, `segment`, `marker`, `fit`, `overrides`; `complete`, `loaded`): Lottie and dotLottie with each platform's player; nothing yet on Windows and Linux ([Lottie](components.md#lottie-animations)) |
| `asuka/svg` | `svg.Icon(markup)`, parsed when the app compiles, and `svg.View(icon) { color }`; `svg.Shape(layers)`: paths the app draws, which follow their box ([SVG icons](components.md#svg-icons)) |
| `asuka/chart` | `chart.View(series) { labels, label, low, high, grid }`; `chart.Series(name:, kind: .bar \| .line \| .area \| .points, values:, smooth:, color:)`: in Asuka, drawn with `svg.Shape` ([Charts](components.md#charts)) |
| `asuka/shader` | `shader.Program(source)`, a Kage shader compiled with the app for every platform, and `shader.Shader(program) { uniforms, animated }` ([Shaders](components.md#shaders)) |
| `asuka/markdown` | `markdown.View(text) { streaming }` and `markdown.heal(text)`: Markdown as the platform's styled text, healed while it streams in ([Markdown](components.md#markdown)) |

Where a platform has no code for a native package (an app run by
`asuka test`, which has none), what needs it throws an `unsupported`
error; `asuka/store` keeps nothing then, and reads give their defaults.

`asuka test` makes runs repeat: `time.now()` starts at
2026-01-01T00:00:00Z and moves only as the test's time does (`sleep`),
the local zone is UTC, and each test has an empty directory of its own
as `fs.appDir()`. On the web the app's directory is the page's, kept in
the browser's storage ([The app's files](web.md#the-apps-files)).

The device's packages (`location`, `sensors`, `purchase`, `pay`,
`appleauth`, `nfc`, `bluetooth`) have fakes for tests: `location.fake()`,
`purchase.fake(products)` and the others give the package an Asuka
implementation a test drives (it moves the device, approves a purchase,
taps a tag, adds a peripheral) in place of the platform's, as the
[standard library's README](../std/README.md#test-fakes) says.

## Packages that aren't here

What the standard library doesn't have comes from a
[package](packages.md) (`asuka add`), or from a native package of your
own ([Native code](native.md)) when it's a platform API nothing wraps.
