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
| `asuka/http` | `http.get`、`http.post`、`http.postJSON`、`http.request`：请求经由平台自己的客户端发出，所以代理、受信任的证书、缓存和 cookie 都和系统里的表现一致。`Response` 有 `status`、`url`、`headers`、`body`、`text()`、`json<T>()` 和 `ok()` |
| `asuka/store` | `store.read(key, default)`、`store.write(key, value)`、`store.remove(key)`：以 JSON 形式跨启动保存的小数据，存放在平台的偏好设置里（UserDefaults、SharedPreferences、`localStorage`，在 Windows 和 Linux 上是一个文件） |
| `asuka/location` | `location.authorization()`（可追踪）、`request()`、`requestAlways()`、`requestPrecise(purpose)`、`current()`、`positions()`（流）、`placeName(lat, lon)`、`openSettings()`、`supported()`；后台位置更新（`startUpdates(…)`、`updates()`、`stopUpdates()`、`updating()`）和区域监控（`monitor(region)`、`crossings()`、`stopMonitoring(id)`、`monitored()`），这些事件会一直保留到被读取，应用处理期间还能保持运行（`hold()`）。基于 Core Location、Android 的 LocationManager、WinRT 的 Geolocator、位置门户或 GeoClue，以及 Geolocation API |
| `asuka/sensors` | `sensors.available(sensor)`；`accelerometer(every:)`、`gyroscope`、`magnetometer`、`motion`（姿态、重力、用户加速度）、`pressure`、`light`、`stepUpdates()`（都是流）；`steps(from:, to:)`；`simulate(json)`。基于 Core Motion、SensorManager、Generic Sensor API 和 iio-sensor-proxy |
| `asuka/purchase` | 应用内购买：`declare(items)`、`products(items)`、`buy(item, options)`、`Transaction.finish()`、`events()`（从应用启动起的流）、`owned()`（可追踪：`has(id)`）、`entitlements()`、`restore()`、`statuses(group)`、`manageSubscriptions(id)`、`redeemCode()`、`requestRefund(t)`、`storefront()`。基于 StoreKit 2、Play Billing、Microsoft Store 和 Digital Goods API |
| `asuka/pay` | Apple Pay 与 Google Pay：`available(request)`、`pay(request)`（配送联系人、配送方式、卡片、优惠码和授权都有异步处理函数，返回 `Update`，或 `throw Invalid(field:, message:)`）、`setUp()`，以及钱包自己的 `PayButton` |
| `asuka/appleauth` | 通过 Apple 登录：`signIn(scopes:, nonce:, state:)`、`credentialState(user)`、`available()`，以及 Apple 的 `SignInButton`。仅限 iOS 和 macOS |
| `asuka/nfc` | NFC 标签：`state()`（可追踪）、`start(alert:, poll:)` 得到 `Session`（`next()`、`setAlert`、`fail`、`stop`）、`Tag`（`message`、`ndef`、`write`、`makeReadOnly`、`transceive`）、`opened()`，以及 NDEF 的 `Message` 和 `Record`（`encode()`、`decode(bytes)`）。基于 Core NFC、Android 的读卡器模式和 Web NFC |
| `asuka/bluetooth` | 低功耗蓝牙：`state()`（可追踪）、`request()`、`scan()`（流）、`choose(filters)`、`connect(device)`（返回 `Connection` store：`state`、`services()`、`characteristic(s, c)`），特征值的 `read`、`write`、`notifications()`；`serve(services)`、`advertise(…)`；以及蓝牙 SIG 定义的 UUID。基于 Core Bluetooth、Android 的 BLE、WinRT、BlueZ 和 Web Bluetooth |

如果某个平台没有原生包的代码（比如由 `asuka test` 运行的应用，它没有任何平台代码），用到它的地方会抛出 `unsupported` 错误；这时 `asuka/store` 什么也不保存，读取时返回默认值。

设备相关的包（`location`、`sensors`、`purchase`、`pay`、`appleauth`、`nfc`、`bluetooth`）都为测试提供了替身：`location.fake()`、`purchase.fake(products)` 等会把一个由测试驱动的 Asuka 实现换给这个包，代替平台的实现（移动设备位置、批准一笔购买、轻触一个标签、添加一个外设），详见[标准库的 README](../../std/README.md#test-fakes)。

## 标准库之外的包

标准库没有的功能，可以来自一个[包](packages.md)（`asuka add`）；如果是还没有人封装过的平台 API，就写一个你自己的原生包（[原生代码](native.md)）。
