# Bluetooth

`asuka/bluetooth` は、react-native-ble-plx や Web Bluetooth と同じように、Bluetooth Low Energy のデバイスとやりとりします。セントラルとしても（デバイスを見つけ、接続し、キャラクタリスティックを読み書きし、その値を追う）、ペリフェラルとしても（サービスを提供し、アドバタイズする）動けます。iOS と macOS では Core Bluetooth、Android では BluetoothLeScanner と GATT、Windows では WinRT の Bluetooth LE、Linux では BlueZ、Chromium では Web Bluetooth を使います。

UUID は文字列です。Bluetooth SIG の 16 ビットの UUID は 16 進数 4 桁（`"180d"`）、それ以外は 128 ビットを小文字で書きます。`bluetooth.uuid(u)` は UUID をこの形に、`bluetooth.fullUUID(u)` は 128 ビットの形に書き直します。よく使う UUID はパッケージに用意されています。`heartRate`、`battery`、`deviceInformation`、`heartRateMeasurement`、`batteryLevel`、`bodySensorLocation`、`deviceName`、`manufacturerName` などです。

## アダプターと権限

`bluetooth.state()` は追跡され、`.on`、`.off`、`.unauthorized`（ユーザーがアプリを許可していない）、`.unsupported`、`.resetting`、`.unknown` のいずれかです。バインディングの中で読むと、ユーザーが Bluetooth をオフにしたり、アプリを許可したりするのに追従します。`bluetooth.request()` は、初回に権限を求め（iOS のプロンプト、Android 12 の「付近のデバイス」）、その時点の状態を返します。

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

## デバイスの検出

`bluetooth.scan(services:, duplicates:)` は、デバイスのアドバタイズを受信するたびに届けるストリームです。各 `Advertisement` には、`device`（その `id` と `name`）、`rssi`、`localName`、`services`、`serviceData`、`manufacturerData`（会社 ID（16 進数 4 桁）ごと）、`txPower`、`connectable` があります。`duplicates` を指定すると、デバイスごとの最初のアドバタイズだけでなく、すべてのアドバタイズを届けます（RSSI を追うときに使います）。`services` を指定すると、そのいずれかをアドバタイズしているデバイスのものだけを届けます。スキャンはストリームが読まれているあいだ続き、`BluetoothError.off`、`denied`、`notAvailable` を投げます。

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

### ブラウザーのデバイス選択画面

ページはスキャンできません（Chromium のフラグを有効にした場合を除きます）。`bluetooth.choose(filters, optionalServices:)` は、いずれかの `Filter`（`services`、`name`、`namePrefix`）に一致するデバイスを並べたブラウザーの選択画面を表示し、ユーザーが選んだデバイスを返します（選ばれなければ `BluetoothError.canceled`）。ページが使えるのは、フィルターのサービスと `optionalServices` です。Windows では、システムのデバイスピッカー（名前でしか絞り込めません）になります。それ以外のプラットフォームでは `notAvailable` を投げるので、代わりにスキャンしてください。

## 接続

`bluetooth.connect(device, timeout:, autoConnect:)` は `Connection` を返します。`Connection` はストアで、作られると接続し、オーナー（それを作ったコンポーネント）が生きているあいだ、または `close()` を呼ぶまで接続を保ちます。`state`（`.connecting`、`.connected`、`.disconnecting`、`.disconnected`）と `error`（切断された理由）は状態なので、画面は変化に合わせてそれを表示できます。

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

- `connection.services()` は、デバイスのサービスとそのキャラクタリスティックです（接続してから得られるので、それまで待ちます）。`connection.characteristic(service, char)` は一つを探します（見つからなければ `BluetoothError.notFound`）。
- `Characteristic` には、`uuid`、`service`、`properties`（`.read`、`.write`、`.writeWithoutResponse`、`.notify`、`.indicate` など）、`descriptors` があります。`read()` は値を読みます。`write(value)` は値を書き込み、デバイスが受け取ってから戻ります。`writeWithoutResponse(value)` は待ちません（書けるのは MTU から 3 を引いた長さまでです）。`notifications()` は値のストリームで、読まれているあいだは通知（またはインディケーション）がオンになります。
- `connection.mtu()` と `connection.rssi()`。

切断された接続の呼び出しは `BluetoothError.disconnected` を投げます。

## ペリフェラルとしての動作

`bluetooth.serve(services)` は、接続してきたセントラルに、タスクがキャンセルされるまで `LocalService` を提供します。`bluetooth.advertise(localName:, services:, …)` は、タスクがキャンセルされるまでアドバタイズします。`LocalCharacteristic` は、読み取りに固定の `value` か `onRead`（セントラルの ID を受け取り、その時点の値を返します）で応え、書き込みを `onWrite` で受け取ります。`notify(value)` は購読しているセントラルに通知し、`subscribers()`（追跡されます）はそのセントラルが誰かを返します。

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

## ビルドが加えるもの

パッケージをインポートしたアプリには、必要なものが与えられます。

| プラットフォーム | 内容 |
| --- | --- |
| iOS | `NSBluetoothAlwaysUsageDescription`（独自の説明文は `ios.usage` に書きます）。asuka.toml の `bluetooth.background` で指定したバックグラウンドモード |
| Android | `BLUETOOTH_SCAN`（`bluetooth.location` がなければ `neverForLocation` 付き）、`BLUETOOTH_CONNECT`、`BLUETOOTH_ADVERTISE`。`android.hardware.bluetooth_le`（必須にはしません） |
| macOS | 利用目的の説明文と、Hardened Runtime の `com.apple.security.device.bluetooth` エンタイトルメント |
| Linux | Flatpak の `--system-talk-name=org.bluez` |

## プラットフォームごとの動作

| | iOS、macOS | Android | Windows | Linux | Web |
| --- | --- | --- | --- | --- | --- |
| スキャン | ✓ | ✓ | ✓（アクティブスキャンなので名前も届きます） | ✓（BlueZ の discovery） | Chromium の実験的なフラグを有効にした場合（`requestLEScan`） |
| 選択画面 | — | — | ✓ システムのデバイスピッカー | — | ✓ `requestDevice` |
| 接続、GATT | ✓ | ✓ | ✓ | ✓ | ✓（選択画面で指定したサービス） |
| サービスの提供 | ✓ | ✓ | ✓ | ✓ | — |
| アドバタイズ | 名前とサービスだけ | ✓ | サービスとメーカーデータ。名前はなし | ✓ | — |
| MTU | システムが決める | 要求する（517） | システムが決める | BlueZ が決める | 23 |
| RSSI | ✓ | ✓ | — | BlueZ がデバイスを受信しているあいだ | — |

Windows では、`request()` は無線へのアクセスを求めます（パッケージ化されていないアプリには、このアクセスがあります）。Linux は権限を求めず、`request()` は `state()` と同じです。bluetoothd やアダプターがなければ `unsupported`、アプリが BlueZ とやりとりすることをバスが許さなければ `unauthorized` になります。Windows はデバイスが切断された理由を伝えないので、接続が失われたときのエラーは、失われたことしか伝えません。Safari と Firefox には Web Bluetooth がなく、unsupported になります。

## テスト

`bluetooth.fake()` は `FakeBluetooth` をパッケージに与えます。`FakeBluetooth` は、アダプター（`setState`）と、テストが追加するペリフェラル（`add(FakePeripheral(id:, name:, rssi:, advertised:, services: [FakeService(uuid:, characteristics: [FakeCharacteristic(uuid:, properties:, value:)])]))`）を持ち、スキャンはそのペリフェラルを受信し、接続はそれにつながります。`notify(device, service, char, value)` は通知を送り、`subscribed(…)` はアプリがそのキャラクタリスティックを購読しているかどうかを返し、`remove(id)` はデバイスを取り除きます（その接続は失われます）。`written` はアプリが書き込んだものの一覧、`scans` と `connected` は動いているものの一覧です。ペリフェラルとしては、`centralRead`、`centralWrite`、`centralSubscribe` がアプリのサービスに対してセントラルの役を演じ、`servers`、`advertising`、`notified`、`lastRead()` がアプリのしたことを示します。

```asuka
test "a strap, heard" {
  let bt = bluetooth.fake()
  bt.add(bluetooth.FakePeripheral(id: "AA:BB:CC:00:00:01", name: "Polar H10", rssi: -58, advertised: ["180d"],
    services: [bluetooth.FakeService(uuid: "180d", characteristics: [bluetooth.FakeCharacteristic(uuid: "2a37", properties: [.notify])])]))
  expect(bluetooth.state() == .on)
}
```
