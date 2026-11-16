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
| `asuka/time` | `time.now()`（自 1970 年以来的纳秒数）、`time.monotonic()`（只会向前走的 `Duration`） |
| `asuka/i18n` | 应用显示的语言：`i18n.locale()`、`setLocale(tag)`、`languages()`、`preferredLanguages()`、`isRTL()`、`autonym(tag)`、`region()`；按这种语言的方式书写数字和日期：`formatNumber`、`formatInt`、`formatPercent`、`formatDate(t, date:, time:)`。应用自己的文案用 `tr.key(…)`（见[多语言](i18n.md)） |
| `asuka/http` | `http.get`、`http.post`、`http.postJSON`、`http.request`：请求经由平台自己的客户端发出，所以代理、受信任的证书、缓存和 cookie 都和系统里的表现一致。`Response` 有 `status`、`url`、`headers`、`body`、`text()`、`json<T>()` 和 `ok()` |
| `asuka/store` | `store.read(key, default)`、`store.write(key, value)`、`store.remove(key)`：以 JSON 形式跨启动保存的小数据，存放在平台的偏好设置里（UserDefaults、SharedPreferences、`localStorage`，在 Windows 和 Linux 上是一个文件） |

如果某个平台没有原生包的代码（比如由 `asuka test` 运行的应用，它没有任何平台代码），用到它的地方会抛出 `unsupported` 错误；这时 `asuka/store` 什么也不保存，读取时返回默认值。

## 标准库之外的包

标准库没有的功能，可以来自一个[包](packages.md)（`asuka add`）；如果是还没有人封装过的平台 API，就写一个你自己的原生包（[原生代码](native.md)）。
