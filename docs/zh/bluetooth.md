# 蓝牙

`asuka/bluetooth` 用来和低功耗蓝牙（Bluetooth Low Energy）设备通信，和 react-native-ble-plx、Web Bluetooth 做的事一样：作为中心设备（central），发现设备、连接设备，读写和订阅设备的特征（characteristic）；作为外围设备（peripheral），提供服务、广播自己。iOS 和 macOS 上用的是 Core Bluetooth；Android 上是 BluetoothLeScanner 和 GATT；Windows 上是 WinRT 的 Bluetooth LE；Linux 上是 BlueZ；Chromium 里则是 Web Bluetooth。

UUID 是字符串：Bluetooth SIG 的 16 位 UUID 写成四位十六进制数字（`"180d"`），其他的写成完整的 128 位，都用小写。`bluetooth.uuid(u)` 把一个 UUID 写成这种形式，`bluetooth.fullUUID(u)` 写成 128 位。包里也为常用的 UUID 起好了名字：`heartRate`、`battery`、`deviceInformation`、`heartRateMeasurement`、`batteryLevel`、`bodySensorLocation`、`deviceName`、`manufacturerName` 等。

## 适配器与权限

`bluetooth.state()` 会被追踪：它是 `.on`、`.off`、`.unauthorized`（用户还没有允许应用使用蓝牙）、`.unsupported`、`.resetting` 或 `.unknown`；在绑定里读取时，它会跟随用户关闭蓝牙或给应用授权。`bluetooth.request()` 在第一次调用时请求权限（iOS 的授权弹窗、Android 12 的“附近的设备”权限），然后返回当时的状态。

```asuka
import asuka/bluetooth

component Adapter {
  let adapter = bluetooth.state()

  Column {
    Text("Bluetooth: {adapter}")
    Button("Allow") { on press { let _ = try? await bluetooth.request() } }
  }
}
```

## 发现设备

`bluetooth.scan(services:, duplicates:)` 是一个流，听到设备的广播就送出来：每个 `Advertisement` 包含 `device`（它的 `id` 和 `name`）、`rssi`、`localName`、`services`、`serviceData`、`manufacturerData`（按厂商 ID 区分，四位十六进制数字）、`txPower` 和 `connectable`。设置 `duplicates` 会送出每一条广播，而不只是每个设备的第一条（用来跟踪 RSSI）；设置 `services` 则只送出广播了其中某个服务的设备。有人读取这个流时它才会扫描；它可能抛出 `BluetoothError.off`、`denied` 或 `notAvailable`。

```asuka
component Scanner {
  state found: [bluetooth.Advertisement] = []

  task {
    do {
      for await a in bluetooth.scan() {
        found.removeWhere(x => x.device.id == a.device.id)
        found.push(a)
      }
    } catch e {
      print(e.message)
    }
  }

  Column {
    for a in found key a.device.id {
      Text("{a.device.name} {a.rssi} dBm")
    }
  }
}
```

### 浏览器的设备选择器

网页不能扫描（除非打开 Chromium 的一个实验性开关）：`bluetooth.choose(filters, optionalServices:)` 会弹出浏览器的选择器，列出符合任意一个 `Filter`（`services`、`name`、`namePrefix`）的设备，并返回用户选中的那个（用户没有选时抛出 `BluetoothError.canceled`）。网页可以使用过滤条件里的服务和 `optionalServices`。在 Windows 上，它是系统的设备选取器（只能按名称过滤）。在其他平台上它会抛出 `notAvailable`：请改用扫描。

## 连接

`bluetooth.connect(device, timeout:, autoConnect:)` 返回一个 `Connection`，它是一个 store：创建时就开始连接，只要它的 owner（创建它的组件）还在，就一直保持连接，或者保持到调用 `close()` 为止。它的 `state`（`.connecting`、`.connected`、`.disconnecting`、`.disconnected`）和 `error`（断开的原因）都是状态，页面可以随着它们的变化来显示。

```asuka
fn heartRate(v: bytes) -> int? {
  let flags = v.get(0) ?? 0
  if v.count() >= 3 && flags % 2 == 1 { return v.get(1)! + v.get(2)! * 256 }
  if v.count() >= 2 { return v.get(1)! }
  return none
}

component HeartRate(device: bluetooth.Device) {
  state bpm = ""
  let connection = bluetooth.connect(device, timeout: 20s)

  task {
    if let ch = try? await connection.characteristic(bluetooth.heartRate, bluetooth.heartRateMeasurement) {
      do {
        for await v in ch.notifications() {
          if let n = heartRate(v) { bpm = "{n} bpm" }
        }
      } catch e {
        bpm = e.message
      }
    }
  }

  Column {
    Text("{connection.state}")
    Text(bpm)
  }
}
```

- `connection.services()` 是设备的服务和服务下的特征（要等连接建立：它会等待）；`connection.characteristic(service, char)` 查找其中一个特征（找不到时抛出 `BluetoothError.notFound`）。
- `Characteristic` 有它的 `uuid`、`service`、`properties`（`.read`、`.write`、`.writeWithoutResponse`、`.notify`、`.indicate` 等）和 `descriptors`。`read()` 读取它的值；`write(value)` 写入值，等设备接收后才返回；`writeWithoutResponse(value)` 不等待（最多 MTU 减 3 个字节）；`notifications()` 是它的值组成的流，有人读取期间会打开它的通知（或指示）。
- `connection.mtu()` 和 `connection.rssi()`。

连接断开后，它的调用都会抛出 `BluetoothError.disconnected`。

## 作为外围设备

`bluetooth.serve(services)` 向连上来的中心设备提供 `LocalService`，直到它的 task 被取消；`bluetooth.advertise(localName:, services:, …)` 会一直广播，直到它的 task 被取消。`LocalCharacteristic` 用固定的 `value` 或者 `onRead`（参数是中心设备的 ID，返回此刻的值）回答读取请求；用 `onWrite` 接收写入；`notify(value)` 通知已订阅的中心设备，`subscribers()`（会被追踪）列出它们是谁。

```asuka
component Thermometer {
  let level = bluetooth.LocalCharacteristic(uuid: bluetooth.batteryLevel, properties: [.read, .notify], permissions: [.readable],
    onRead: _ => bytes([87]))

  task {
    await all(serving([bluetooth.LocalService(uuid: bluetooth.battery, characteristics: [level])]), advertising())
  }

  Column {
    Text("{level.subscribers().count()} subscribed")
    Button("Notify") { on press { level.notify(bytes([86])) } }
  }
}

async fn serving(s: [bluetooth.LocalService]) { try? await bluetooth.serve(s) }
async fn advertising() { try? await bluetooth.advertise(localName: "Thermo", services: [bluetooth.battery]) }
```

## 在后台

应用不在前台时，它的连接会继续保持，扫描也可以继续：

- `bluetooth.backgroundScan(services, duplicates:)` 扫描广播了其中某个服务的设备，应用进入后台后也不会停。iOS 在后台只能按服务扫描，频率更低，每个设备也只上报一次；调用了它的应用会自动获得 iOS 的 `bluetooth-central` 模式。Android 上它是一个由系统运行的扫描，应用进程退出后仍会继续（不指定服务的扫描在灭屏时会被 Android 暂停）。读取它的一方停止时，扫描也随之停止。桌面平台只在应用运行时扫描；Web 上和 `scan` 一样抛出 `notAvailable`。
- 应用有后台模式时（来自 asuka.toml 的 `bluetooth.background`，或者 `backgroundScan` 带来的模式），iOS 为了回收内存结束应用时，会替它保留连接和扫描；等到其中有了新消息（设备发来通知、扫描听到了设备），再在后台重新启动应用，不显示界面。Android 的后台扫描则会在应用进程不在时，把听到的内容先保存下来。
- `bluetooth.restored()` 是一个流，给出系统带回来的东西，即一个 `Restored`：`devices` 是 iOS 保留的连接（重新连接它们即可，已经连上的会立即就绪）；`scanServices` 是 iOS 保留的扫描所针对的服务（可以用 `backgroundScan` 重新扫描这些服务，或者把它停掉）；`advertisements` 是后台扫描在应用没有扫描读取时听到的广播。这次启动带回来的内容排在最前面，并一直保留到有人读取：应用启动时就在根组件的一个 task 里读取它。
- 应用拿到后台扫描听到的内容之后，`bluetooth.stopBackgroundScan()` 停掉上一次运行留下的扫描（Android 上是进程退出后仍在运行的那个，iOS 上是恢复回来的那个）。本次运行自己的后台扫描不受影响；它们中的任何一个启动时也会取代旧的扫描。

```asuka
state heard: [string] = []

async fn followRestored() throws {
  for await r in bluetooth.restored() {
    for a in r.advertisements { heard.push(a.device.id) }
    if !r.scanServices.isEmpty() { bluetooth.stopBackgroundScan() }
  }
}

async fn scanningAway() throws {
  for await a in bluetooth.backgroundScan([bluetooth.heartRate]) {
    heard.push(a.device.id)
  }
}

component Straps {
  task { try? await followRestored() }
  task { try? await scanningAway() }

  Column {
    for id in heard key id { Text(id) }
  }
}
```

用户在多任务界面里把应用划掉之后，iOS 不会再重新启动它；Android 的扫描会在重启设备、或者用户强行停止应用时停止。

## 构建时添加的内容

导入这个包的应用会得到它需要的东西：

| 平台 | 内容 |
| --- | --- |
| iOS | `NSBluetoothAlwaysUsageDescription`（在 `ios.usage` 里写上你自己的文案）；asuka.toml 的 `bluetooth.background` 声明的后台模式，调用了 `backgroundScan` 的应用还会得到 `bluetooth-central`；有后台模式时，还有这个包的启动类，它会在启动结束前创建 iOS 要恢复的管理器 |
| Android | `BLUETOOTH_SCAN`（除非设置了 `bluetooth.location`，否则带 `neverForLocation`）、`BLUETOOTH_CONNECT`、`BLUETOOTH_ADVERTISE`；`android.hardware.bluetooth_le`，不是必需的；调用了 `backgroundScan` 的应用还会得到后台扫描的接收器 |
| macOS | 授权文案，以及加固运行时（hardened runtime）的 `com.apple.security.device.bluetooth` entitlement |
| Linux | Flatpak 的 `--system-talk-name=org.bluez` |

## 各平台的情况

| | iOS、macOS | Android | Windows | Linux | Web |
| --- | --- | --- | --- | --- | --- |
| 扫描 | ✓ | ✓ | ✓（主动扫描：能拿到名称） | ✓（BlueZ 的设备发现） | 只能在打开 Chromium 的实验性开关后使用（`requestLEScan`） |
| 设备选择器 | — | — | ✓ 系统的设备选取器 | — | ✓ `requestDevice` |
| 连接、GATT | ✓ | ✓ | ✓ | ✓ | ✓（选择器里的服务） |
| 提供服务 | ✓ | ✓ | ✓ | ✓ | — |
| 广播 | 只有名称和服务 | ✓ | 服务和厂商数据，没有名称 | ✓ | — |
| MTU | 系统决定 | 向设备申请（517） | 系统决定 | BlueZ 决定 | 23 |
| RSSI | ✓ | ✓ | — | 仅在 BlueZ 听得到设备时 | — |
| 后台扫描 | iOS：只按服务，频率更低；macOS：仅在应用运行时 | ✓ 由系统运行，进程退出后继续 | 仅在应用运行时 | 仅在应用运行时 | — |
| `restored()` | iOS：连接、扫描及扫描听到的内容 | 后台扫描听到的内容 | — | — | — |

在 Windows 上，`request()` 请求的是对无线电的访问权限（未打包的应用本来就有）；Linux 不请求权限，`request()` 就等于 `state()`。没有 bluetoothd 或者没有适配器时是 `unsupported`；系统总线不允许应用和 BlueZ 通信时是 `unauthorized`。Windows 不会说明设备为什么断开：连接丢失时，错误只说明连接丢失了。Safari 和 Firefox 没有 Web Bluetooth：状态是 unsupported。

## 测试

`bluetooth.fake()` 给这个包换上一个 `FakeBluetooth`：它有一个适配器（`setState`），以及测试添加的外围设备（`add(FakePeripheral(id:, name:, rssi:, advertised:, services: [FakeService(uuid:, characteristics: [FakeCharacteristic(uuid:, properties:, value:)])]))`），扫描能听到它们，连接也能连上它们。`notify(device, service, char, value)` 发送一条通知，`subscribed(…)` 说明应用是否订阅了某个特征，`remove(id)` 把设备移走（它的连接随之丢失），`written` 列出应用写入的内容，`scans` 和 `connected` 列出正在进行的扫描和连接。作为外围设备时，`centralRead`、`centralWrite` 和 `centralSubscribe` 扮演中心设备来测试应用的服务；`servers`、`advertising`、`notified` 和 `lastRead()` 说明应用做了什么。`restore(Restored(…))` 模拟系统为蓝牙启动应用（`restored()` 会给出它，没有读取方时留给第一个读取方）；`backgroundScans` 是正在进行的后台扫描数，`backgroundLeft` 表示是否还留着上一次运行的扫描（`restore` 带回扫描或扫描听到的内容时会留下一个；应用自己的后台扫描或 `stopBackgroundScan()` 会结束它）。

```asuka
test "a strap, heard" {
  let bt = bluetooth.fake()
  bt.add(bluetooth.FakePeripheral(id: "AA:BB:CC:00:00:01", name: "Polar H10", rssi: -58, advertised: ["180d"],
    services: [bluetooth.FakeService(uuid: "180d", characteristics: [bluetooth.FakeCharacteristic(uuid: "2a37", properties: [.notify])])]))
  expect(bluetooth.state() == .on)
}
```
