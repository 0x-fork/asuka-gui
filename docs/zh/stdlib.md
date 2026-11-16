# 标准库

标准库就是 `asuka/...` 这些包。它们随 `asuka` 命令行工具一起提供，不需要 `asuka add`：导入就能用。

```asuka
import asuka/http
import asuka/store

state units = store.read("units", "metric")
```

大多数包不需要平台代码：它们用 Asuka 写成，Asuka 表达不了的部分由运行时自己的 C 完成，所以在每个平台上的行为都一样（`asuka/json` 和 `asuka/math` 是编译器的一部分）。其余的和别的[原生包](native.md)一样：每个平台用自己的 API 来实现（iOS 和 macOS 上用 URLSession，Android 上用它自己的类，Web 上用浏览器的 API，Windows 和 Linux 上用 C），所以它们的行为和平台原生应用里的一致。

| 包 | 内容 |
|---|---|
| `asuka/integrity` | `integrity.supported()`、`generateKey()`、`attestKey(id, challenge)`、`assertion(id, data)`（iOS 和 Mac 上的 App Attest）；`prepare(cloudProjectNumber:)`、`token(requestHash:)`（Play Integrity） |
| `asuka/js` | `js.eval(script, timeout:)`：在平台自己的引擎里离屏运行 JavaScript（WKWebView、Android 的 WebView、WebView2、WebKitGTK、沙盒化的 frame），以 JSON 返回它的值；`js.JsError.timeout` |
| `asuka/json` | `json.decode<T>(text)` 和 `json.encode(value, indent:)`：带类型的 JSON，依据编译器对 `T` 的了解来读写 |
| `asuka/math` | `sqrt`、`pow`、`floor`、`ceil`、`round`、`sin`、`cos`、`atan2`、`clamp`、`random`、`randomInt`、`seed`、`pi` |
| `asuka/time` | `time.now()`、`time.date(2026, 3, 7, hour: 9, zone: .utc)`、`time.unix(s)`、`time.parse(rfc3339)`：得到一个 `Time`，即带时区（`.local`、`.utc`、`.fixed(seconds:)`）的时刻，以及它在挂钟上的 `year`、`month`、`day`、`hour`、`minute`、`second`、`nanosecond`、`weekday`。它的方法有：`format("EEE d MMM, HH:mm")`（Unicode 的日期模式，英文输出）、`rfc3339()`、`add(d)`、`sub(t)`（得到 `Duration`）、`addDate(years:, months:, days:)`、`isBefore`、`isAfter`、`compare`、`sameDay`、`startOfDay`、`truncate(d)`、`inZone`、`utc`、`local`、`unix`、`unixMilli`、`unixNano`。另有 `time.monotonic()`（只会向前走的 `Duration`）、`time.since(t)`、`time.ticks(1s)` 和 `time.ticker(1s)`（时刻的流） |
| `asuka/url` | `url.parse(text)`：得到 `URL?`，含 `scheme`、`user`、`host`、`port`、`path`、`rawQuery`、`fragment`，以及 `query(name)`、`queryAll(name)`、`text()` 和 `resolve(ref)`（RFC 3986）；还有 `url.resolve(base, ref)`、`escape`、`unescape`、`pathEscape`、`pathUnescape`、`encodeQuery(map)`（参数名排序）、`parseQuery(raw)` |
| `asuka/regex` | `regex.compile(pattern)`（模式有误时抛出错误）：得到 `Regex`，有 `matches`、`find`、`findAll`、`findRange`、`findSubmatch`、`findAllSubmatches`、`replace(text, with: "$1")`、`replaceEach(text, f:)`、`split`、`groupNames`；以及 `regex.quote(text)`。语法是 RE2 的，匹配时间是线性的（不回溯） |
| `asuka/encoding` | `base64Encode`、`base64URLEncode`（不补齐）、`base64Decode`（两种字母表都接受）、`hexEncode`、`hexDecode` |
| `asuka/crypto` | `sha256`、`sha512`、`sha1`、`hmacSHA256(key:, data:)`、`randomBytes(n)`（来自系统的随机源）、`uuid()`（第 4 版）、`equal(a, b)`（常数时间比较），以及 Ed25519（RFC 8032）：`ed25519Verify(publicKey, message, signature)`、`ed25519Sign(privateKey, message)`、`ed25519PublicKey(privateKey)`、`ed25519NewKey()` |
| `asuka/html` | `escape(text)` 和 `unescape(text)`（数字引用，以及文本中常用的命名引用） |
| `asuka/random` | `random.int(1, 6)`（包含两端）、`float()`、`chance(p)`、`pick(items)`、`shuffled(items)`、`seed(n)` |
| `asuka/fs` | `fs.appDir()`（应用自己的目录）、`join([…])`、`clean`、`dir`、`base`、`ext`；以及会阻塞、应放在 `spawn { }` 里调用的：`readDir`（返回 `Entry`：`name`、`isDir`、`size`、`modified`）、`stat`、`read`、`readText`、`write`、`writeText`、`append`、`appendText`、`copy`、`rename`、`mkdirAll`、`remove`、`removeAll`、`exists`。失败时抛出 `FsError`（`.notFound(path)`、`.exists`、`.denied`、`.notDirectory`、`.isDirectory`、`.notEmpty`、`.failed(path, reason)`） |
| `asuka/apps` | `apps.list()`、`apps.search(apps, query)`（启动器式的排序：完全相同、开头相同、某个单词的开头或各单词首字母、任意位置、字母按顺序出现；不区分大小写和重音）、`apps.icon(app, size)`（一个 PNG 文件的路径）、`apps.open(app)`：macOS、Windows 和 Linux 上已安装的应用；在手机和网页上抛出 `AppsError.unsupported` |
| `asuka/dock` | `dock.setProgress(Progress(value:, state:))` 和 `dock.requestAttention(.critical)`：应用图标上的进度和提醒，在 macOS 的程序坞、Windows 的任务栏和 Linux 的启动器上（桌面环境提供 Unity 启动器 API 时）；在手机和网页上什么也不做 |
| `asuka/hotkey` | `hotkey.Hotkey(Shortcut(key:, primary:, shift:, alt:, control:, mods:)) { description; on press; on error(e) }`：显示期间有效的全局快捷键（RegisterEventHotKey、RegisterHotKey、GlobalShortcuts 门户）；在手机和网页上报 `.notAvailable` |
| `asuka/tray` | `tray.Tray(id) { image, symbol, template, title, tooltip, menu: [Action]; on click }`：显示期间放在托盘里的图标（Mac 的菜单栏、Windows 的通知区域、StatusNotifierItem）；`tray.DockMenu { items }`：程序坞图标的菜单、跳转列表的任务、启动器的快捷列表；`tray.anchor(id)`：浮出 `Window` 的锚点；`async supported()`。在手机和网页上什么也不显示 |
| `asuka/update` | 桌面应用的自动更新，Sparkle 式：`update.UpdateWindow { icon:, interval:, automaticChecks: }`（后台检查；安装更新、跳过此版本、稍后提醒、版本说明、下载进度、立即重新启动），`update.AppMenu()` 和 `update.CheckForUpdates()`（给 `MenuBar` 用的“检查更新…”），`checkForUpdates()`；`check()`（返回 `Release` 或 none）、`Release.install(progress:)`、`relaunch()`、`unsupported()`、`updater`（更新器状态，一个 store）、`setChannel`、`setFeed`、`compareVersions`、`onInstalled`，以及用户的选择（`setAutomaticChecks`、`setAutomaticDownloads`、`lastCheck`）；测试用的 `fakeHost`。更新用 `asuka update keygen` 的密钥签名，增量包由核心应用；下载和安装由宿主完成。在手机上返回 `.notAvailable`；Web 上则重新加载页面（见[发布](shipping.md#自动更新)） |
| `asuka/i18n` | 应用显示的语言：`i18n.locale()`、`setLocale(tag)`、`languages()`、`preferredLanguages()`、`isRTL()`、`autonym(tag)`、`region()`；按这种语言的方式书写数字和日期：`formatNumber`、`formatInt`、`formatPercent`、`formatDate(t, date:, time:)`。应用自己的文案用 `tr.key(…)`（见[多语言](i18n.md)） |
| `asuka/agerange` | `agerange.request(13, 18)`：不需要出生日期就能得到用户的年龄段（iOS 26 的 Declared Age Range、Play 的 Age Signals）：一个 `Range`（`status`、`lower`、`upper`、`source`）；其他平台上是 `AgeRangeError.notAvailable` |
| `asuka/appearance` | `appearance.current()`（会被追踪：`accent` 为 `Color?`、`increaseContrast`、`reduceTransparency`；只有在有东西读取时才监视设置）、`appearance.now()`、`appearance.changes()`（一个流）。强调色来自 Mac、Windows、Linux 的设置门户、Android 的 Material You、CSS 的 AccentColor；iOS 没有 |
| `asuka/biometric` | `biometric.check()`（`kind`、`enrolled`、`passcode`）、`biometric.authenticate(reason:, cancel:, noPasscode:)`：LocalAuthentication、BiometricPrompt、Windows Hello、WebAuthn 的平台认证器；`BiometricError.canceled`、`.notAvailable`、`.failed` |
| `asuka/device` | `device.info()`（`model`、`system`、`systemVersion`、`appVersion`、`build`）、`device.browse(url)`（在应用内打开：SFSafariViewController、Custom Tab）、`device.pickPhotos(max:)`、`device.pickFiles(types:, multiple:)`（系统的选择器，无需权限；副本放在应用的缓存里）、`device.clipboardText()`、`clipboardHas()`、`clipboardImage()`、`copyImage(path)`、`clipboardChanges()`（一个流）、`device.keepAwake(on)` |
| `asuka/dialog` | `dialog.open(...)`、`dialog.save(name:, ...)`、`dialog.chooseFolder(...)`、`chooseFolders`：系统的文件对话框（NSOpenPanel 和 NSSavePanel、通用项目对话框、GtkFileDialog、File System Access 选择器），返回用户自己的路径；`dialog.Filter(name:, types:, extensions:)`。手机上，open 是文档选择器 |
| `asuka/http` | `http.get`、`http.post`、`http.postJSON`、`http.request`、`http.download`（边收边写进文件，并报告进度）、`http.loopback`（回环接口上的服务器，接收登录的重定向）：请求经由平台自己的客户端发出，所以代理、受信任的证书、缓存和 cookie 都和系统里的表现一致。`Response` 有 `status`、`url`、`headers`、`body`、`text()`、`json<T>()` 和 `ok()` |
| `asuka/login` | `login.set(on)`（在 macOS 上，用户允许之前抛出 `LoginError.needsApproval`）、`login.enabled()`、`login.openedAtLogin()`：用户登录时启动应用（SMAppService、Run 注册表项、XDG 自动启动）；手机上的应用做不到 |
| `asuka/network` | `network.status()`（会被追踪：`connected`、`type`、`metered`、`constrained`；只有在有人读取时才监听网络）、`network.current()`、`network.changes()`（一个流）。iOS 和 macOS 上用 NWPathMonitor，Android 上用 ConnectivityManager，Windows 上用网络列表管理器，Linux 上用 GNetworkMonitor 和 NetworkManager，网页上用 `navigator.onLine` 和 Network Information API |
| `asuka/platform` | `platform.call(module, method, args)` 和 `platform.send`：按名称调用宿主自己的模块，参数采用各模块的小端编码（`u8`、`u32`、`f64`、`str`、`readU32`、`readStr` 等）；`platform.next(module, method)` 是模块长轮询给出的内容组成的流；`platform.unsupported(e)` 判断宿主是否表示没有这个模块。基于宿主模块的那些包（notify、push、icloud、photos 等）都建立在它之上。供标准库的包使用；应用要调用自己的平台代码，请用[原生包](native.md) |
| `asuka/recent` | `recent.add(path)`、`recent.clear()`、`recent.list()`：用户最近打开的文档，出现在“最近使用”菜单和程序坞菜单、跳转列表、最近使用的文件中 |
| `asuka/store` | `store.read(key, default)`、`store.write(key, value)`、`store.remove(key)`、`store.flush()`、`store.dir()`：以 JSON 形式跨启动保存的小数据，存放在平台的偏好设置里（UserDefaults、SharedPreferences、`localStorage`，在 Windows 和 Linux 上是一个文件，改动后稍等片刻才写入） |
| `asuka/notify` | 本地通知：`request()`、`show(n)`、`schedule(n)`（在 `n.after` 之后显示）、`cancel(id)`、`showing()`、`remove(ids)`、`removeThread(t)`、`removeAll()`、`setBadge(n)`；`setCategories`（操作、回复）、`setChannels`、`setChannelGroups`（Android）；`Notification(…).from(person, conversation)`（消息）；`present(decide)`（应用在前台时通知怎么显示）；`responses()`（点按、操作和回复组成的流；`r.defer()` 让应用继续运行，直到这项工作完成）。各平台用的都是宿主自己的通知：UserNotifications、Android 的通知、Windows 的 toast、Linux 的通知、浏览器的通知 |
| `asuka/push` | `push.register()`（令牌：APNs 的、FCM 的、Web 的订阅）、`push.opened()` 和 `push.received()`（`Message` 组成的流；`Message.from(person, conversation)`） |
| `asuka/watch` | `watch.path(dir, ignore:, recursive:, attributes:)` 返回一个 `Watch`：它的 `root`，以及 `events`，一个由一批批 `Event` 组成的流（`op`：create、write、remove、rename、attrib、overflow；`path`、`oldPath`）。基于 FSEvents、inotify 和 ReadDirectoryChangesW（internal/fswatch）；手机和 Web 上不支持 |
| `asuka/background` | `background.start(title:, text:, progress:, actions:, onAction:)` 返回一个 `Task`（`update(…)`、`end()`）：在后台继续进行的长时间工作（Android 的前台服务及其通知；iOS 上多给约 30 秒；桌面端不让系统休眠） |
| `asuka/calendar` | `calendar.request()`（`Access`）、`calendars()`、`events(from:, to:, calendars:)`、`add(Event)`（系统的表单）：EventKit、Calendar Provider |
| `asuka/compose` | `compose.mail(to:, cc:, bcc:, subject:, body:, html:, attachments:)` 和 `compose.message(to:, body:, …)` 显示系统的编写界面，返回一个 `Result`（cancelled、sent、saved、unknown）；`check()` |
| `asuka/contacts` | `contacts.pick()`（系统的选择器：返回一个 `Contact` 或 none）、`request()`、`list(name:, limit:)`、`add(Contact)` |
| `asuka/library` | `library.save(path, collection:, name:, folder:, mime:, move:)`：把文件存进照片、相册、下载或音乐，并返回存到了哪里 |
| `asuka/liveactivity` | `liveactivity.start(Content)` 返回一个 `Activity`（`update(c, alert:)`、`end(c, dismiss:)`）、`events()`（开始、结束、推送令牌）、`enabled()`：iOS 的实时活动、Android 16 的实时更新 |
| `asuka/periodic` | `periodic.schedule(name, every:, network:, charging:)`、`cancel(name)`、`runs(name)`（`Run` 组成的流，`run.finish(ok)`）：iOS 的后台任务、WorkManager |
| `asuka/photos` | 经授权访问照片图库：`request()`、`current()`、`list(screenshots:, offset:, limit:)`、`thumbnail(id, size:)`、`export(id)`、`manageLimited()` |
| `asuka/widget` | `widget.set(kind, Entry)`、`setAll(kind, entries)`、`clear(kind)`：应用的小组件显示的内容 |
| `asuka/crash` | `crash.reports()`（一个流：先是已保存的报告，之后每来一个就给一个；`Report`：`id`、`kind`、`fatal`、`time`、`message`、`text`、`threads`、带面包屑和值的 `context`）、`breadcrumb(message)`、`set(key, value)`、`record(e)`、`delete(id)`。运行记录在桌面上由宿主保存（sdk/crash），在 iOS、Android 和 Web 上由核心保存（核心自己的致命错误、未捕获的异常、iOS 的 MetricKit 诊断、Android 进程的退出原因、未捕获的 JavaScript 错误） |
| `asuka/icloud` | iOS 和 macOS 上的 iCloud：`account()` 和 `files(scope)`（会被跟踪：iCloud 改动它们时随之更新）、`Value(key, default)`（保存在 iCloud 键值存储里的 store）、`documents()`、`container(id)`、`readFile`、`writeFile`、`delete`、`rename`、`download`、`evict`、`conflicts(path)`、`resolve(path, keeping:)`。其他平台：没有账户、列表为空、“icloud: not available” |
| `asuka/document` | `document.open(path, read:, write:)` 返回一个 `Document<T>` store：`content`、`state`（loading、saved、edited、saving、conflict、failed）、`error`；`edit(c)`（稍后自动保存）、`save()`、`saveAnyway()`、`revert()`。在 iOS 和 macOS 上对文件做协调访问。基于文档的应用的窗口、菜单和文档浏览器（`Documents<T>`）要等 UI 运行时支持窗口和导航后才有 |
| `asuka/sqlite` | `sqlite.open(name)`（应用数据目录里的一个文件、一个绝对路径，或 `":memory:"`）返回一个 `DB`：`migrate(steps)`、`exec(sql, args)` 和 `query<T>(sql, args)`（异步，在主线程之外运行；查询到的行直接解码成 `T`，结构体的字段按列名对应）、`watch<T>(sql, args)`（一个 `Live<T>` store：`rows`、`loading`、`error`；参数变化时、它读取的表被写入时都会重新运行）、`changed()`、`close()`。SQLite 是 sqlite 插件的库（sdk/sqlite），随应用一起分发；在 Web 上运行在页面的一个 worker 里 |
| `asuka/storage` | `storage.Value(key, default)`：一个 store，它的 `value` 保存在设备上（用的是 asuka/store），会被跟踪，变化时写入；`reset()`；`storage.flush()`、`storage.dirName()` |
| `asuka/power` | `power.status()`（会被追踪：`battery`、`onBattery`、`level`、`charging`、`lowPower`、`thermal`）、`power.events()`、`suspends()`、`resumes()`、`locks()`、`unlocks()`（流）、`power.idleTime()`：IOKit 和 ProcessInfo、UIDevice、BatteryManager 和 PowerManager、GetSystemPowerStatus 和会话通知、UPower 和 logind、Battery Status API |
| `asuka/preview` | `preview.show(sources, start:, edit:)`：在系统的查看器里显示文件（路径、资源、URL）（iOS 和 Mac 上是快速查看；其他平台上是文件对应的应用；Web 上是新标签页）；`preview.PreviewError.noViewer` |
| `asuka/print` | `print.file(source, job:, orientation:)`、`print.html(markup, ...)`：系统的打印对话框，返回 `print.Outcome`；`print.pdf(markup, page:, landscape:, margins:)`：把标记生成 PDF，返回它的路径；`print.Page.a4`、`letter`……、`print.regionPage(locale)`。它的名字和 prelude 的 `print` 相同：`import asuka/print as printing` |
| `asuka/review` | `review.request()`：商店的评分提示（iOS 和 Mac 上的 StoreKit、Play 的应用内评价），是否显示由商店的配额决定；Windows、Linux 和 Web 上什么也不做。`review.appStoreURL(id)`、`review.playStoreURL(id)` 用于“给我们评分”按钮 |
| `asuka/screen` | `screen.displays()`（会被追踪：`id`、`name`、`bounds`、`workArea`、`scale`、`refreshRate`、`primary`）、`screen.primary`、`nearest`、`matching`；`screen.lock(orientation)`、`screen.setBars(style:, hideStatus:, hideNavigation:)`、`screen.preventCapture(on)`、`screen.screenshots()`（一个流） |
| `asuka/secure` | `secure.read(key)`、`secure.write(key, value)`、`secure.remove(key)`：由平台加密的少量机密（钥匙串、Android Keystore、凭据管理器、通过 libsecret 访问的用户密钥环、用不可导出的 WebCrypto 密钥加密的 IndexedDB） |
| `asuka/shell` | `shell.reveal(path)`、`shell.open(path)`、`shell.trash(path)`、`shell.beep()`：文件管理器提供的功能（NSWorkspace、资源管理器和回收站、GIO 和 FileManager1）；`shell.ShellError.noApp` |
| `asuka/splash` | `splash.hold()`、`splash.hide()`：在应用准备好之前保持 asuka.toml 的启动画面（iOS、Android；其他平台什么也不做） |
| `asuka/tracking` | `tracking.request()`、`tracking.current()`：iOS 和 Mac 上 App Tracking Transparency 的提示（`.notAsked`、`.restricted`、`.denied`、`.allowed`）；Windows 的广告 ID 设置；浏览器的 Global Privacy Control 或 Do Not Track；Android 和 Linux 没有提示，返回 `.allowed` |
| `asuka/webauth` | `webauth.open(url, callbackScheme:, ephemeral:)`：在系统的界面里登录（ASWebAuthenticationSession、Custom Tab；WebView2 和 WebKitGTK 窗口；弹出窗口），返回回调 URL；`WebAuthError.canceled`；取消任务会关闭它 |
| `asuka/location` | `location.authorization()`（可追踪）、`request()`、`requestAlways()`、`requestPrecise(purpose)`、`current()`、`positions()`（流）、`placeName(lat, lon)`、`openSettings()`、`supported()`；后台位置更新（`startUpdates(…)`、`updates()`、`stopUpdates()`、`updating()`）和区域监控（`monitor(region)`、`crossings()`、`stopMonitoring(id)`、`monitored()`），这些事件会一直保留到被读取，应用处理期间还能保持运行（`hold()`）。基于 Core Location、Android 的 LocationManager、WinRT 的 Geolocator、位置门户或 GeoClue，以及 Geolocation API |
| `asuka/sensors` | `sensors.available(sensor)`；`accelerometer(every:)`、`gyroscope`、`magnetometer`、`motion`（姿态、重力、用户加速度）、`pressure`、`light`、`stepUpdates()`（都是流）；`steps(from:, to:)`；`simulate(json)`。基于 Core Motion、SensorManager、Generic Sensor API 和 iio-sensor-proxy |
| `asuka/purchase` | 应用内购买：`declare(items)`、`products(items)`、`buy(item, options)`、`Transaction.finish()`、`events()`（从应用启动起的流）、`owned()`（可追踪：`has(id)`）、`entitlements()`、`restore()`、`statuses(group)`、`manageSubscriptions(id)`、`redeemCode()`、`requestRefund(t)`、`storefront()`。基于 StoreKit 2、Play Billing、Microsoft Store 和 Digital Goods API |
| `asuka/pay` | Apple Pay 与 Google Pay：`available(request)`、`pay(request)`（配送联系人、配送方式、卡片、优惠码和授权都有异步处理函数，返回 `Update`，或 `throw Invalid(field:, message:)`）、`setUp()`，以及钱包自己的 `PayButton` |
| `asuka/appleauth` | 通过 Apple 登录：`signIn(scopes:, nonce:, state:)`、`credentialState(user)`、`available()`，以及 Apple 的 `SignInButton`。仅限 iOS 和 macOS |
| `asuka/nfc` | NFC 标签：`state()`（可追踪）、`start(alert:, poll:)` 得到 `Session`（`next()`、`setAlert`、`fail`、`stop`）、`Tag`（`message`、`ndef`、`write`、`makeReadOnly`、`transceive`）、`opened()`，以及 NDEF 的 `Message` 和 `Record`（`encode()`、`decode(bytes)`）。基于 Core NFC、Android 的读卡器模式和 Web NFC |
| `asuka/bluetooth` | 低功耗蓝牙：`state()`（可追踪）、`request()`、`scan()`（流）、`choose(filters)`、`connect(device)`（返回 `Connection` store：`state`、`services()`、`characteristic(s, c)`），特征值的 `read`、`write`、`notifications()`；`serve(services)`、`advertise(…)`；以及蓝牙 SIG 定义的 UUID。基于 Core Bluetooth、Android 的 BLE、WinRT、BlueZ 和 Web Bluetooth |
| `asuka/audio` | `audio.play(source)`，以及 `audio.Player(source)`：一个 store，有 `play`、`pause`、`seek`、`setVolume`、`setRate`、`setLoop`、`status()` 和 `release()`。基于 AVPlayer、Media3 的 ExoPlayer、`<audio>`、MCI、GStreamer（[设备](device.md)） |
| `asuka/nowplaying` | `nowplaying.NowPlaying`：应用正在播放的内容，显示在锁屏和系统的媒体控件上，以及它们发回的命令（媒体键、耳机的按键）；给它一个 `audio.Player` 时会跟随它（[设备](device.md)） |
| `asuka/speech` | `speech.speak(text, options:)`、`stop`、`pause`、`resume`、`speaking`、`voices()`：用系统的语音朗读文本（[设备](device.md)） |
| `asuka/record` | `record.audio(maxDuration:, quality:, background:)` 以及 `stopAudio`、`pauseAudio`、`resumeAudio`、`audioStatus()`：录制麦克风的声音；`record.start()` 和 `stop()`：录制屏幕上 `camera.View` 的视频（[设备](device.md)） |
| `asuka/camera` | `camera.View`（`facing`、`mode`、`scan`、`codes`；`code`、`error`）：实时的相机画面；`camera.capture()`、`camera.take()`（系统的相机界面）、`camera.record()`（[设备](device.md)） |
| `asuka/scan` | `scan.file(path, kinds:)`：识别图片里的条形码和二维码（[设备](device.md)） |
| `asuka/vision` | `vision.analyze(path, text:, faces:, codes:, languages:)`：在设备上识别图片里的文字（逐行、逐词）、人脸和码，以及它们各自的位置（[设备](device.md)） |
| `asuka/transcribe` | `transcribe.check(language)`、`transcribe.file(path, language:)`：把录音转成文字，只在设备上进行（做不到的地方返回 `notAvailable`：Web、Linux、较旧的 Android）（[设备](device.md)） |
| `asuka/imageedit` | `imageedit.edit(source, actions, format:, quality:)`（缩放、裁剪、旋转、翻转、填充、像素化）、`imageedit.size`、`imageedit.videoFrame`：处理图片和视频帧，输出 JPEG 或 PNG（[设备](device.md)） |
| `asuka/maps` | `maps.View`（`bind region`、`markers`、`mapType`、`showsUser`、`interactive`；`markerPress`、`press`、`regionChange`）：Apple 地图、Google 地图、OpenStreetMap（[设备](device.md)） |
| `asuka/lottie` | `lottie.View(source)`（`bind playing`、`bind progress`、`loop`、`speed`、`segment`、`marker`、`fit`、`overrides`；`complete`、`loaded`）：用各平台的播放器播放 Lottie 和 dotLottie；Windows 和 Linux 上暂时没有（[组件](components.md)） |
| `asuka/svg` | `svg.Icon(markup)` 在应用编译时解析，`svg.View(icon) { color }` 显示它；`svg.Shape(layers)`：应用自己画的路径，会跟随它的框（[组件](components.md)） |
| `asuka/chart` | `chart.View(series) { labels, label, low, high, grid }`；`chart.Series(name:, kind: .bar \| .line \| .area \| .points, values:, smooth:, color:)`：用 Asuka 写成，通过 `svg.Shape` 绘制（[组件](components.md)） |
| `asuka/shader` | `shader.Program(source)`：随应用一起为每个平台编译的 Kage 着色器；`shader.Shader(program) { uniforms, animated }`（[组件](components.md)） |
| `asuka/markdown` | `markdown.View(text) { streaming }` 和 `markdown.heal(text)`：把 Markdown 显示为平台自己的富文本，流式输入时边接收边修补（[组件](components.md)） |

如果某个平台没有原生包的代码（比如由 `asuka test` 运行的应用，它没有任何平台代码），用到它的地方会抛出 `unsupported` 错误；这时 `asuka/store` 什么也不保存，读取时返回默认值。

`asuka test` 让每次运行都可重复：`time.now()` 从 2026-01-01T00:00:00Z 开始，只随测试的时间（`sleep`）前进，本地时区是 UTC，每个测试都有一个属于自己的空目录作为 `fs.appDir()`。Web 上应用还没有自己的目录，所以 `asuka/fs` 的调用在那里会抛出错误。

设备相关的包（`location`、`sensors`、`purchase`、`pay`、`appleauth`、`nfc`、`bluetooth`）都为测试提供了替身：`location.fake()`、`purchase.fake(products)` 等会把一个由测试驱动的 Asuka 实现换给这个包，代替平台的实现（移动设备位置、批准一笔购买、轻触一个标签、添加一个外设），详见[标准库的 README](../../std/README.md#test-fakes)。

## 标准库之外的包

标准库没有的功能，可以来自一个[包](packages.md)（`asuka add`）；如果是还没有人封装过的平台 API，就写一个你自己的原生包（[原生代码](native.md)）。
