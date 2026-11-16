# 标准库

标准库就是 `asuka/...` 这些包。它们随 `asuka` 命令行工具一起提供，不需要 `asuka add`：导入就能用。

```asuka
import asuka/http
import asuka/store

state units = store.read("units", "metric")
```

其中一些包属于编译器的一部分：它们只是普通的函数，没有平台代码。其余的和别的[原生包](native.md)一样：每个平台用自己的 API 来实现（iOS 和 macOS 上用 URLSession，Android 上用它自己的类，Web 上用浏览器的 API，Windows 和 Linux 上用 Go），所以它们的行为和平台原生应用里的一致。

| 包 | 内容 |
|---|---|
| `asuka/json` | `json.decode<T>(text)` 和 `json.encode(value, indent:)`：带类型的 JSON，依据编译器对 `T` 的了解来读写 |
| `asuka/math` | `sqrt`、`pow`、`floor`、`ceil`、`round`、`sin`、`cos`、`atan2`、`clamp`、`random`、`randomInt`、`seed`、`pi` |
| `asuka/time` | `time.now()`、`time.date(2026, 3, 7, hour: 9, zone: .utc)`、`time.unix(s)`、`time.parse(rfc3339)`：得到一个 `Time`，即带时区（`.local`、`.utc`、`.fixed(seconds:)`）的时刻，以及它在挂钟上的 `year`、`month`、`day`、`hour`、`minute`、`second`、`nanosecond`、`weekday`。它的方法有：`format("EEE d MMM, HH:mm")`（Unicode 的日期模式，英文输出）、`rfc3339()`、`add(d)`、`sub(t)`（得到 `Duration`）、`addDate(years:, months:, days:)`、`isBefore`、`isAfter`、`compare`、`sameDay`、`startOfDay`、`truncate(d)`、`inZone`、`utc`、`local`、`unix`、`unixMilli`、`unixNano`。另有 `time.monotonic()`（只会向前走的 `Duration`）、`time.since(t)`、`time.ticks(1s)` 和 `time.ticker(1s)`（时刻的流） |
| `asuka/url` | `url.parse(text)`：得到 `URL?`，含 `scheme`、`user`、`host`、`port`、`path`、`rawQuery`、`fragment`，以及 `query(name)`、`queryAll(name)`、`text()` 和 `resolve(ref)`（RFC 3986）；还有 `url.resolve(base, ref)`、`escape`、`unescape`、`pathEscape`、`pathUnescape`、`encodeQuery(map)`（参数名排序）、`parseQuery(raw)` |
| `asuka/regex` | `regex.compile(pattern)`（模式有误时抛出错误）：得到 `Regex`，有 `matches`、`find`、`findAll`、`findRange`、`findSubmatch`、`findAllSubmatches`、`replace(text, with: "$1")`、`replaceEach(text, f:)`、`split`、`groupNames`；以及 `regex.quote(text)`。语法是 RE2 的，匹配时间是线性的（不回溯） |
| `asuka/encoding` | `base64Encode`、`base64URLEncode`（不补齐）、`base64Decode`（两种字母表都接受）、`hexEncode`、`hexDecode` |
| `asuka/crypto` | `sha256`、`sha1`、`hmacSHA256(key:, data:)`、`randomBytes(n)`（来自系统的随机源）、`uuid()`（第 4 版）、`equal(a, b)`（常数时间比较） |
| `asuka/html` | `escape(text)` 和 `unescape(text)`（数字引用，以及文本中常用的命名引用） |
| `asuka/random` | `random.int(1, 6)`（包含两端）、`float()`、`chance(p)`、`pick(items)`、`shuffled(items)`、`seed(n)` |
| `asuka/fs` | `fs.appDir()`（应用自己的目录）、`join([…])`、`clean`、`dir`、`base`、`ext`；以及会阻塞、应放在 `spawn { }` 里调用的：`readDir`（返回 `Entry`：`name`、`isDir`、`size`、`modified`）、`stat`、`read`、`readText`、`write`、`writeText`、`append`、`appendText`、`copy`、`rename`、`mkdirAll`、`remove`、`removeAll`、`exists`。失败时抛出 `FsError`（`.notFound(path)`、`.exists`、`.denied`、`.notDirectory`、`.isDirectory`、`.notEmpty`、`.failed(path, reason)`） |
| `asuka/apps` | `apps.list()`、`apps.search(apps, query)`（启动器式的排序：完全相同、开头相同、某个单词的开头或各单词首字母、任意位置、字母按顺序出现；不区分大小写和重音）、`apps.icon(app, size)`（一个 PNG 文件的路径）、`apps.open(app)`：macOS、Windows 和 Linux 上已安装的应用；在手机和网页上抛出 `AppsError.unsupported` |
| `asuka/dock` | `dock.setProgress(Progress(value:, state:))` 和 `dock.requestAttention(.critical)`：应用图标上的进度和提醒，在 macOS 的程序坞、Windows 的任务栏和 Linux 的启动器上（桌面环境提供 Unity 启动器 API 时）；在手机和网页上什么也不做 |
| `asuka/hotkey` | `hotkey.Hotkey(hotkey.Shortcut(key:, primary:, shift:, alt:, control:)) { description; on press; on error(e) }`：显示期间有效的全局快捷键（RegisterEventHotKey、RegisterHotKey、GlobalShortcuts 门户）；在手机和网页上报 `.notAvailable` |
| `asuka/i18n` | 应用显示的语言：`i18n.locale()`、`setLocale(tag)`、`languages()`、`preferredLanguages()`、`isRTL()`、`autonym(tag)`、`region()`；按这种语言的方式书写数字和日期：`formatNumber`、`formatInt`、`formatPercent`、`formatDate(t, date:, time:)`。应用自己的文案用 `tr.key(…)`（见[多语言](i18n.md)） |
| `asuka/http` | `http.get`、`http.post`、`http.postJSON`、`http.request`：请求经由平台自己的客户端发出，所以代理、受信任的证书、缓存和 cookie 都和系统里的表现一致。`Response` 有 `status`、`url`、`headers`、`body`、`text()`、`json<T>()` 和 `ok()` |
| `asuka/login` | `login.set(on)`（在 macOS 上，用户允许之前抛出 `LoginError.needsApproval`）、`login.enabled()`、`login.openedAtLogin()`：用户登录时启动应用（SMAppService、Run 注册表项、XDG 自动启动）；手机上的应用做不到 |
| `asuka/network` | `network.status()`（会被追踪：`connected`、`type`、`metered`、`constrained`；只有在有人读取时才监听网络）、`network.current()`、`network.changes()`（一个流）。iOS 和 macOS 上用 NWPathMonitor，Android 上用 ConnectivityManager，Windows 上用网络列表管理器，Linux 上用 GNetworkMonitor 和 NetworkManager，网页上用 `navigator.onLine` 和 Network Information API |
| `asuka/platform` | `platform.call(module, method, args)` 和 `platform.send`：按名称调用宿主自己的模块，参数采用各模块的小端编码（`u8`、`u32`、`f64`、`str`、`readU32` 等）。供标准库的包使用；应用要调用自己的平台代码，请用[原生包](native.md) |
| `asuka/recent` | `recent.add(path)`、`recent.clear()`、`recent.list()`：用户最近打开的文档，出现在“最近使用”菜单和程序坞菜单、跳转列表、最近使用的文件中 |
| `asuka/store` | `store.read(key, default)`、`store.write(key, value)`、`store.remove(key)`、`store.flush()`、`store.dir()`：跨启动保存的小数据，以 JSON 存在平台的偏好设置里（UserDefaults、SharedPreferences、`localStorage`，Windows 和 Linux 上是一个文件，在修改后稍过片刻写入） |
| `asuka/notify` | 本地通知：`request()`、`show(n)`、`schedule(n)`（在 `n.after` 之后）、`cancel(id)`、`showing()`、`remove(ids)`、`removeThread(t)`、`removeAll()`、`setBadge(n)`；`setCategories`（操作、回复）、`setChannels`、`setChannelGroups`（Android）；`Notification(…).from(person, conversation)`（消息）；`present(decide)`（应用在前台时通知如何显示）；`responses()`（点按、操作和回复的流；`r.defer()` 让应用继续运行以完成工作）。每个平台上用的都是宿主自己的通知：UserNotifications、Android 的通知、Windows 的 toast、Linux 的通知、浏览器的通知 |
| `asuka/push` | `push.register()`（令牌：APNs 的、FCM 的、Web 的订阅）、`push.opened()` 和 `push.received()`（`Message` 的流；`Message.from(person, conversation)`） |
| `asuka/watch` | `watch.path(dir, ignore:, recursive:, attributes:)` 得到一个 `Watch`：它的 `root`，以及 `events`，即一批批 `Event` 组成的流（`op`：create、write、remove、rename、attrib、overflow；`path`、`oldPath`）。基于 FSEvents、inotify 和 ReadDirectoryChangesW（internal/fswatch）；手机和 Web 上不支持 |
| `asuka/background` | `background.start(title:, text:, progress:, actions:, onAction:)` 得到一个 `Task`（`update(…)`、`end()`）：由系统显示的长时间工作（Android 前台服务的通知、iOS 的持续处理、桌面端的进度） |
| `asuka/calendar` | `calendar.request()`（`Access`）、`calendars()`、`events(from:, to:, calendars:)`、`add(Event)`（系统的表单）：EventKit、Calendar Provider |
| `asuka/compose` | `compose.mail(to:, cc:, bcc:, subject:, body:, html:, attachments:)` 和 `compose.message(to:, body:, …)` 打开系统的撰写界面，并返回一个 `Result`（cancelled、sent、saved、unknown）；`check()` |
| `asuka/contacts` | `contacts.pick()`（系统的选择器：一个 `Contact` 或 none）、`request()`、`list(name:, limit:)`、`add(Contact)` |
| `asuka/library` | `library.save(path, collection:, name:, folder:, mime:, move:)`：把文件存进照片、图库、下载或音乐；返回它存到了哪里 |
| `asuka/liveactivity` | `liveactivity.start(Content)` 得到一个 `Activity`（`update(c, alert:)`、`end(c, dismiss:)`）、`events()`（开始、结束、推送令牌）、`enabled()`：iOS 的实时活动、Android 16 的实时更新 |
| `asuka/periodic` | `periodic.schedule(name, every:, network:, charging:)`、`cancel(name)`、`runs(name)`（`Run` 的流，`run.finish(ok)`）：iOS 的后台任务、WorkManager |
| `asuka/photos` | 经用户许可的照片库：`request()`、`current()`、`list(screenshots:, offset:, limit:)`、`thumbnail(id, size:)`、`export(id)`、`manageLimited()` |
| `asuka/widget` | `widget.set(kind, Entry)`、`setAll(kind, entries)`、`clear(kind)`：应用的小组件显示的内容 |
| `asuka/crash` | `crash.reports()`（一个流：先是已保存的报告，之后每来一份就给一份；`Report`：`id`、`kind`、`fatal`、`time`、`message`、`text`、`threads`，以及带面包屑和值的 `context`）、`breadcrumb(message)`、`set(key, value)`、`record(e)`、`delete(id)`。记录由桌面端的宿主保存（sdk/crash）；手机和 Web 上还没有（reports 为空） |
| `asuka/icloud` | iOS 和 macOS 上的 iCloud：`account()` 和 `files(scope)`（会被追踪：随 iCloud 的变化而更新）、`Value(key, default)`（保存在 iCloud 键值存储里的 store）、`documents()`、`container(id)`、`readFile`、`writeFile`、`delete`、`rename`、`download`、`evict`、`conflicts(path)`、`resolve(path, keeping:)`。在其他平台上：没有账户，列表为空，报错 "icloud: not available" |
| `asuka/document` | `document.open(path, read:, write:)` 得到一个 `Document<T>` store：`content`、`state`（loading、saved、edited、saving、conflict、failed）、`error`；`edit(c)`（稍后自动保存）、`save()`、`saveAnyway()`、`revert()`。在 iOS 和 macOS 上会协调文件访问。基于文档的应用所需的窗口、菜单和文档浏览器（`Documents<T>`）要等 UI 运行时的窗口和导航就绪 |
| `asuka/host` | 上面这些包所依赖的：宿主自己的模块（`host.call(module, method, args)`、`host.next(module, method)`）；应用不需要直接用它 |
| `asuka/sqlite` | `sqlite.open(name)`（应用数据目录里的文件、绝对路径，或 `":memory:"`）得到一个 `DB`：`migrate(steps)`、`exec(sql, args)` 和 `query<T>(sql, args)`（异步，不在主线程上执行；行直接解码成 `T`，按列名对应结构体的字段）、`watch<T>(sql, args)`（一个 `Live<T>` store：`rows`、`loading`、`error`；参数变化时、它读取的表被写入时会重新执行）、`changed()`、`close()`。SQLite 来自 sqlite 插件的库（sdk/sqlite），随应用一起发布；Web 上暂不支持 |
| `asuka/storage` | `storage.Value(key, default)`：一个 store，它的 `value` 保存在设备上（用的是 asuka/store），会被追踪，变化时写入；`reset()`；`storage.flush()`、`storage.dirName()` |
| `asuka/integrity` | `integrity.supported()`、`generateKey()`、`attestKey(id, challenge)`、`assertion(id, data)`（iOS 和 Mac 上的 App Attest）；`prepare(cloudProjectNumber:)`、`token(requestHash:)`（Play Integrity） |
| `asuka/js` | `js.eval(script, timeout:)`：在平台自己的引擎里离屏运行 JavaScript（WKWebView、Android 的 WebView、WebView2、WebKitGTK、沙盒化的 frame），以 JSON 返回它的值；`js.JsError.timeout` |
| `asuka/agerange` | `agerange.request(13, 18)`：不需要出生日期就能得到用户的年龄段（iOS 26 的 Declared Age Range、Play 的 Age Signals）：一个 `Range`（`status`、`lower`、`upper`、`source`）；其他平台上是 `AgeRangeError.notAvailable` |
| `asuka/appearance` | `appearance.current()`（会被追踪：`accent` 为 `Color?`、`increaseContrast`、`reduceTransparency`；只有在有东西读取时才监视设置）、`appearance.now()`、`appearance.changes()`（一个流）。强调色来自 Mac、Windows、Linux 的设置门户、Android 的 Material You、CSS 的 AccentColor；iOS 没有 |
| `asuka/biometric` | `biometric.check()`（`kind`、`enrolled`、`passcode`）、`biometric.authenticate(reason:, cancel:, noPasscode:)`：LocalAuthentication、BiometricPrompt、Windows Hello、WebAuthn 的平台认证器；`BiometricError.canceled`、`.notAvailable`、`.failed` |
| `asuka/device` | `device.info()`（`model`、`system`、`systemVersion`、`appVersion`、`build`）、`device.browse(url)`（在应用内打开：SFSafariViewController、Custom Tab）、`device.pickPhotos(max:)`、`device.pickFiles(types:, multiple:)`（系统的选择器，无需权限；副本放在应用的缓存里）、`device.clipboardText()`、`clipboardHas()`、`clipboardImage()`、`copyImage(path)`、`clipboardChanges()`（一个流）、`device.keepAwake(on)` |
| `asuka/dialog` | `dialog.open(...)`、`dialog.save(name:, ...)`、`dialog.chooseFolder(...)`、`chooseFolders`：系统的文件对话框（NSOpenPanel 和 NSSavePanel、通用项目对话框、GtkFileDialog、File System Access 选择器），返回用户自己的路径；`dialog.Filter(name:, types:, extensions:)`。手机上，open 是文档选择器 |
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

如果某个平台没有原生包的代码（比如由 `asuka test` 运行的应用，它没有任何平台代码），用到它的地方会抛出 `unsupported` 错误；这时 `asuka/store` 什么也不保存，读取时返回默认值。

## 标准库之外的包

标准库没有的功能，可以来自一个[包](packages.md)（`asuka add`）；如果是还没有人封装过的平台 API，就写一个你自己的原生包（[原生代码](native.md)）。
