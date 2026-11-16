# NFC 标签

`platform/nfc` 用来读写 NFC 标签：标签上的 NDEF 消息、标签本身，以及发给标签的原始命令，和 react-native-nfc-manager、Web NFC 做的事一样。iOS 上是带系统面板的 Core NFC；Android 上是为前台页面开启的读卡器模式（reader mode）；Web 上是 Chrome for Android 的 Web NFC。Mac、PC 和 Linux 都没有 NFC。

```asuka
import platform/nfc

component ReadTag {
  state link = ""

  Column {
    Button("Read a Tag") {
      on press {
        do {
          let s = try await nfc.start(alert: "Hold your iPhone near the tag.")
          defer { s.stop() }
          let t = try await s.next()
          link = t.message?.uri() ?? "No link on this tag"
        } catch e {
          link = e.message
        }
      }
    }
    Text(link)
  }
}
```

## 有没有 NFC，开没开

`nfc.state()` 会被追踪：它的 `supported` 表示设备能读取标签，`enabled` 表示 NFC 已打开（即 Android 的设置；iPhone 的 NFC 始终是打开的）。在绑定里读取它时，它会跟随用户打开或关闭 NFC。`nfc.openSettings()` 打开 Android 的 NFC 设置；在其他平台上它会抛出 `NfcError.notAvailable`。

```asuka
component NfcState {
  let st = nfc.state()

  Column {
    Text(if !st.supported { "This device can't read NFC tags." } else if !st.enabled { "NFC is off." } else { "NFC is on." })
    if st.supported && !st.enabled {
      Button("Turn NFC On") { on press { try? await nfc.openSettings() } }
    }
  }
}
```

## 读取标签：会话

`nfc.start(…)` 开始一个寻找标签的会话并返回它；`session.next()` 等待一个标签。会话会一直寻找，直到它被停止（`stop()`）、失败（`fail(message)`：iOS 的面板会显示这条消息），或者在 iOS 上用户关掉了面板（`next()` 抛出 `NfcError.canceled`）或过了六十秒（`timeout`）。开始另一个会话会停止之前的那个。

- `alert` 是寻找标签时 iOS 面板上显示的文字，`setAlert` 可以修改它（Android 和 Web 不显示面板：应用自己展示界面）。
- `poll` 决定寻找什么：`[]` 表示带 NDEF 消息的标签（iOS 的 NFCNDEFReaderSession，这样找到的标签没有 ID，也不能接收命令）；`[.a, .b, .f, .v]` 表示这些类型的标签，可以拿到 ID，也能发送命令（iOS 的 NFCTagReaderSession，ISO 7816 卡需要 asuka.toml 的 `nfc.iso7816`，FeliCa 需要 `nfc.felica`）。Web 只能读取 NDEF。
- `skipNDEF` 让系统在发现标签时不读取消息（Android：对只接收命令的标签更快）；`noSound` 不播放提示音（Android）；`presenceDelay` 是 Android 检查标签是否还在的时间间隔。

会话没能开始时，它会抛出 `notAvailable`、`disabled`（Android 的 NFC 关着）或 `denied`（Web 的权限）。

### 一次一个标签

会话一次只把一个标签交给应用：再次调用 `next()` 会寻找下一个。应用持有一个标签期间，可以读写它。

## NDEF 消息和记录

标签的 `message` 是它的 NDEF 消息，在发现标签时读取（为 `none` 表示没有 NDEF，或者设置了 `skipNDEF`）。消息由 `records` 组成，每条都是一个 `Record`：

- `.uri(u)`：一个 URI（well-known 类型 "U"，前缀会被缩写）；
- `.text(t, language:)`：一段文本；
- `.mime(type:, data:)`：某种 MIME 类型的数据；
- `.external(type:, data:)`：某个外部类型（`domain:name`）的数据；
- `.androidApp(package)`：Android 应用记录（Android 会打开这个包对应的应用）；
- `.raw(tnf:, type:, id:, payload:)`：其他任何记录，保持原样。

`message.uri()` 是第一条记录的 URI，`message.text()` 是第一条文本；`message.encode()` 是消息的字节，也就是标签上保存的样子，`nfc.decode(bytes)` 则把这些字节读回来（失败时抛出 `NfcError.badMessage`）。

## 写入标签和设为只读

`tag.write(message)` 写入一条消息（可格式化的标签会先被格式化）；`tag.ndef` 说明能否写入（`writable()`；`format`：`.notNDEF`、`.formatable`、`.readWrite`、`.readOnly`；`capacity` 是以字节计的容量，未知时为 -1）。它可能抛出 `readOnly`、`tooLarge`、`tagLost`。`tag.makeReadOnly()` 永久锁定标签；`tag.read()` 重新读取消息。

```asuka
async fn writeLink(u: string) throws {
  let s = try await nfc.start(alert: "Hold your iPhone near a tag to write it.")
  defer { s.stop() }
  let t = try await s.next()
  if !t.ndef.writable() {
    s.fail("This tag can't be written.")
    return
  }
  try await t.write(nfc.Message(records: [nfc.Record.uri(u)]))
  s.setAlert("Written.")
}
```

## 标签本身：ID、技术类型和命令

标签的 `id` 是它的标识符（UID，用 `id.hex(" ")` 显示；iOS 的 NDEF 会话里没有），`techs` 是它具备的技术类型，用的是 Android 的名字（`nfcA`、`isoDep`、`mifareUltralight`、`felica`、`iso15693`、`ndef` 等）。`tag.transceive(data)` 发送一条原始命令并返回响应：发给 ISO-DEP 卡的 ISO 7816 APDU、MIFARE 或 NFC-A 命令、FeliCa 命令（不含长度字节）、ISO 15693 请求（标志和命令代码在前）。它需要一个设置了 `poll` 的会话。

## 能打开应用的标签

应用没有打开时，标签也能打开它：

- **iOS** 会在后台读取标签，当标签的第一条记录是某个应用的通用链接（asuka.toml 的 `links`）时，提示用户打开该应用。这个链接会像其他链接一样交给应用的 `openURL`。
- **Android** 会为 asuka.toml 中 `nfc.ndef` 和 `nfc.tech` 列出的标签打开应用。带 URI 的标签交给 `openURL`；其他标签交给 `nfc.opened()`，这是一个 `Tag` 的流，标签还在附近时，应用依然可以和它通信。启动应用的那个标签会一直等到第一个读取者出现。

```asuka
async fn followOpened() throws {
  for await t in nfc.opened() {
    print("opened by {t.techs.joined(separator: ", ")}")
  }
}
```

## asuka.toml 与权限

导入了 `platform/nfc` 的应用会获得 Android 的 `NFC` 权限，以及 iOS 上的 `NFCReaderUsageDescription`（在 `ios.usage` 里写你自己的说明）和 Core NFC 的 entitlement `com.apple.developer.nfc.readersession.formats`（`NDEF` 和 `TAG`）。发布构建的 App ID 需要开启 NFC Tag Reading 能力。

asuka.toml 的 `nfc` 用来补充应用的标签所需的配置：

```toml
[nfc]
iso7816 = ["D2760000850101"]
felica = ["8008"]
ndef = [
  {uri = "https://example.com/t/"},
  {mime = "application/vnd.example.ticket"},
  {external = "example.com:ticket"},
]
tech = [["IsoDep"], ["NfcA", "MifareUltralight"]]
required = true
```

| 键 | 说明 |
| --- | --- |
| `iso7816` | iOS 发现卡片时选择的 ISO 7816 应用（AID，十六进制） |
| `felica` | iOS 轮询的 FeliCa 系统代码 |
| `ndef` | 在 Android 上按第一条记录打开应用的标签：`uri`、`mime`、`external` |
| `tech` | 在 Android 上按技术类型打开应用的标签 |
| `required` | 应用只面向带 NFC 的手机：Google Play 不会向其他设备提供它 |

## 各平台上的表现

| 平台 | 做法 |
| --- | --- |
| iOS | Core NFC：不设置 `poll` 时用 NFCNDEFReaderSession，设置时用 NFCTagReaderSession；系统面板及其提示文字；ISO 7816、MIFARE、FeliCa 和 ISO 15693 命令；通过通用链接在后台读取标签。没有 NFC 开关 |
| Android | 为前台的 Activity 开启读卡器模式；NDEF、NdefFormatable、ISO-DEP、NFC-A/B/F/V 和 MIFARE Ultralight；能打开应用的标签；`openSettings` |
| Web | Web NFC 的 `NDEFReader`（Chrome for Android，需要 https）：读写 NDEF、`makeReadOnly`、标签的序列号。不支持原始命令，查不到容量，标签也不能打开页面。其他浏览器：`notAvailable` |
| macOS、Windows、Linux | 应用用不了 NFC：`state()` 报告不支持，`start` 抛出 `notAvailable` |

## 用模拟标签测试

`nfc.fake()` 给这个包换上一个 `FakeNfc`：会话正在寻找标签时（`session` 不为 none，带有它的 `alert`），`tap(tag)` 把一个 `FakeTag` 交给它（包括它的 `id`、`ndef` 字节、`readOnly`、`formatable`、`capacity`、`techs`，以及按十六进制命令给出的 `answers`，用来回答原始命令）；`cancel()` 模拟用户关掉 iOS 的面板；`open(tag)` 模拟系统用一个标签打开应用；`setEnabled(on)` 打开或关闭 NFC。`tags` 是被靠近过的标签，保持应用处理后的样子（`writes`、它们的 `ndef`），`sessions` 和 `settings` 记录了应用发起的调用。

```asuka
test "a link, read" {
  let n = nfc.fake()
  n.setEnabled(true)
  expect(n.tags.isEmpty())
  let m = nfc.Message(records: [nfc.Record.uri("https://example.com/hello")])
  expect((try nfc.decode(m.encode())).uri() == "https://example.com/hello")
}
```
