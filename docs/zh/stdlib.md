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
| `asuka/http` | `http.get`、`http.post`、`http.postJSON`、`http.request`：请求经由平台自己的客户端发出，所以代理、受信任的证书、缓存和 cookie 都和系统里的表现一致。`Response` 有 `status`、`url`、`headers`、`body`、`text()`、`json<T>()` 和 `ok()` |
| `asuka/login` | `login.set(on)`（在 macOS 上，用户允许之前抛出 `LoginError.needsApproval`）、`login.enabled()`、`login.openedAtLogin()`：用户登录时启动应用（SMAppService、Run 注册表项、XDG 自动启动）；手机上的应用做不到 |
| `asuka/network` | `network.status()`（会被追踪：`connected`、`type`、`metered`、`constrained`；只有在有人读取时才监听网络）、`network.current()`、`network.changes()`（一个流）。iOS 和 macOS 上用 NWPathMonitor，Android 上用 ConnectivityManager，Windows 上用网络列表管理器，Linux 上用 GNetworkMonitor 和 NetworkManager，网页上用 `navigator.onLine` 和 Network Information API |
| `asuka/platform` | `platform.call(module, method, args)` 和 `platform.send`：按名称调用宿主自己的模块，参数采用各模块的小端编码（`u8`、`u32`、`f64`、`str`、`readU32` 等）。供标准库的包使用；应用要调用自己的平台代码，请用[原生包](native.md) |
| `asuka/recent` | `recent.add(path)`、`recent.clear()`、`recent.list()`：用户最近打开的文档，出现在“最近使用”菜单和程序坞菜单、跳转列表、最近使用的文件中 |
| `asuka/store` | `store.read(key, default)`、`store.write(key, value)`、`store.remove(key)`：以 JSON 形式跨启动保存的小数据，存放在平台的偏好设置里（UserDefaults、SharedPreferences、`localStorage`，在 Windows 和 Linux 上是一个文件） |

如果某个平台没有原生包的代码（比如由 `asuka test` 运行的应用，它没有任何平台代码），用到它的地方会抛出 `unsupported` 错误；这时 `asuka/store` 什么也不保存，读取时返回默认值。

## 标准库之外的包

标准库没有的功能，可以来自一个[包](packages.md)（`asuka add`）；如果是还没有人封装过的平台 API，就写一个你自己的原生包（[原生代码](native.md)）。
