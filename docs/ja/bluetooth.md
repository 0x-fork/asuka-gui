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

`bluetooth.scan(services:, duplicates:)` は、デバイスのアドバタイズを受信するたびに届けるストリームです。各 `Advertisement` には、`device`（その `id` と `name`）、`rssi`、`localName`、`services`、`serviceData`、`manufacturerData`（会社 ID（16 進数 4 桁）ごと）、`txPower`、`connectable`、受信した時刻 `time`（`time.Time`）があります。`duplicates` を指定すると、デバイスごとの最初のアドバタイズだけでなく、すべてのアドバタイズを届けます（RSSI を追うときに使います）。`services` を指定すると、そのいずれかをアドバタイズしているデバイスのものだけを届けます。スキャンはストリームが読まれているあいだ続き、`BluetoothError.off`、`denied`、`notAvailable` を投げます。

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

## バックグラウンドでの動作

アプリが見えなくなっても接続は続き、スキャンも続けられます。

- `bluetooth.backgroundScan(services, duplicates:)` は、指定したサービスのどれかをアドバタイズしているデバイスを、アプリがバックグラウンドにあるあいだもスキャンします。iOS はバックグラウンドではサービスを指定したスキャンしかできず、頻度も下がり、各デバイスは一度しか報告されません。これを呼ぶアプリには iOS の `bluetooth-central` モードが与えられます。Android ではシステムが実行するスキャンになり、アプリのプロセスが終了したあとも続きます（サービスを指定しないスキャンは、画面がオフのあいだ Android が一時停止します）。読み手がいなくなるとスキャンも止まります。デスクトップではアプリの実行中だけスキャンし、Web では `scan` と同じく `notAvailable` を投げます。
- バックグラウンドモード（asuka.toml の `bluetooth.background`、または `backgroundScan` によるもの）があると、iOS はメモリーを空けるためにアプリを終了させるときも、その接続とスキャンを保持します。そして、デバイスからの通知やスキャンでの受信といった知らせがあると、画面を出さずにアプリをもう一度起動します。Android のバックグラウンドスキャンは、アプリのプロセスがないあいだに受信したものを保存しておきます。
- `bluetooth.restored()` は、システムが戻してきたもの（`Restored`）のストリームです。`devices` は iOS が保持していた接続で、もう一度接続すれば使えます（接続済みのものはすぐにつながります）。`scanServices` は iOS が保持していたスキャンのサービスで、`backgroundScan` でもう一度スキャンするか、止めます。`advertisements` は、アプリのスキャンが読んでいないあいだにバックグラウンドスキャンが受信したものです。起動時に戻ってきたものが最初に届き、読まれるまで保持されます。アプリの起動時に、ルートのタスクで読んでください。
- `bluetooth.stopBackgroundScan()` は、前回の実行が残したスキャン（Android ではプロセスの終了後も続いていたもの、iOS では復元されたもの）を、受信した内容を受け取ったあとに止めます。今回の実行のバックグラウンドスキャンは続き、それを始めた場合も残っていたスキャンは置き換えられます。

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

ユーザーがアプリスイッチャーでアプリを上にスワイプして終了させると、iOS はアプリをもう起動しません。Android のスキャンは、端末の再起動や、ユーザーがアプリを強制停止したときに止まります。

## ビルドが加えるもの

パッケージをインポートしたアプリには、必要なものが与えられます。

| プラットフォーム | 内容 |
| --- | --- |
| iOS | `NSBluetoothAlwaysUsageDescription`（独自の説明文は `ios.usage` に書きます）。asuka.toml の `bluetooth.background` で指定したバックグラウンドモードと、`backgroundScan` を呼ぶアプリには `bluetooth-central`。モードがあれば、iOS が復元するマネージャーを起動の完了前に作る、パッケージの起動クラス |
| Android | `BLUETOOTH_SCAN`（`bluetooth.location` がなければ `neverForLocation` 付き）、`BLUETOOTH_CONNECT`、`BLUETOOTH_ADVERTISE`。`android.hardware.bluetooth_le`（必須にはしません）。`backgroundScan` を呼ぶアプリには、バックグラウンドスキャンのレシーバー |
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
| バックグラウンドスキャン | iOS：サービス指定のみで頻度は低め。macOS：アプリの実行中だけ | ✓ システムが実行し、プロセスの終了後も続く | アプリの実行中だけ | アプリの実行中だけ | — |
| `restored()` | iOS：接続、スキャンとその受信内容 | バックグラウンドスキャンの受信内容 | — | — | — |

Windows では、`request()` は無線へのアクセスを求めます（パッケージ化されていないアプリには、このアクセスがあります）。Linux は権限を求めず、`request()` は `state()` と同じです。bluetoothd やアダプターがなければ `unsupported`、アプリが BlueZ とやりとりすることをバスが許さなければ `unauthorized` になります。Windows はデバイスが切断された理由を伝えないので、接続が失われたときのエラーは、失われたことしか伝えません。Safari と Firefox には Web Bluetooth がなく、unsupported になります。

## テスト

`bluetooth.fake()` は `FakeBluetooth` をパッケージに与えます。`FakeBluetooth` は、アダプター（`setState`）と、テストが追加するペリフェラル（`add(FakePeripheral(id:, name:, rssi:, advertised:, services: [FakeService(uuid:, characteristics: [FakeCharacteristic(uuid:, properties:, value:)])]))`）を持ち、スキャンはそのペリフェラルを受信し、接続はそれにつながります。`notify(device, service, char, value)` は通知を送り、`subscribed(…)` はアプリがそのキャラクタリスティックを購読しているかどうかを返し、`remove(id)` はデバイスを取り除きます（その接続は失われます）。`written` はアプリが書き込んだものの一覧、`scans` と `connected` は動いているものの一覧です。ペリフェラルとしては、`centralRead`、`centralWrite`、`centralSubscribe` がアプリのサービスに対してセントラルの役を演じ、`servers`、`advertising`、`notified`、`lastRead()` がアプリのしたことを示します。`restore(Restored(…))` は、システムが Bluetooth のためにアプリを起動したところを再現します（`restored()` がそれを返し、読み手がまだいなければ最初の読み手のために保持します）。`backgroundScans` は動いているバックグラウンドスキャンの数で、`backgroundLeft` は前回の実行のスキャンが残っているかどうかです（`restore` がスキャンかその受信内容を戻すと残り、アプリのバックグラウンドスキャンか `stopBackgroundScan()` で終わります）。

```asuka
test "a strap, heard" {
  let bt = bluetooth.fake()
  bt.add(bluetooth.FakePeripheral(id: "AA:BB:CC:00:00:01", name: "Polar H10", rssi: -58, advertised: ["180d"],
    services: [bluetooth.FakeService(uuid: "180d", characteristics: [bluetooth.FakeCharacteristic(uuid: "2a37", properties: [.notify])])]))
  expect(bluetooth.state() == .on)
}
```
