# 標準ライブラリ

標準ライブラリは `asuka/...` のパッケージです。`asuka` CLI に付属しているので、`asuka add` は要りません。インポートすればそのまま使えます。

```asuka
import asuka/http
import asuka/store

state units = store.read("units", "metric")
```

一部はコンパイラーの一部で、プラットフォームのコードを持たないただの関数です。それ以外はほかの[ネイティブパッケージ](native.md)と同じで、各プラットフォームがそれぞれの API で動かします（iOS と macOS では URLSession、Android では Android 自身のクラス、Web ではブラウザーの API、Windows と Linux では Go）。そのため、そのプラットフォームのアプリと同じように振る舞います。

| パッケージ | 内容 |
|---|---|
| `asuka/json` | `json.decode<T>(text)` と `json.encode(value, indent:)`：コンパイラーが `T` について知っていることにもとづく、型付きの JSON |
| `asuka/math` | `sqrt`、`pow`、`floor`、`ceil`、`round`、`sin`、`cos`、`atan2`、`clamp`、`random`、`randomInt`、`seed`、`pi` |
| `asuka/time` | `time.now()`（1970 年からのナノ秒）、`time.monotonic()`（前にしか進まない `Duration`） |
| `asuka/http` | `http.get`、`http.post`、`http.postJSON`、`http.request`：リクエストはプラットフォーム自身のクライアントを通るので、プロキシ、信頼された証明書、キャッシュ、Cookie はシステムと同じように働きます。`Response` には `status`、`url`、`headers`、`body`、`text()`、`json<T>()`、`ok()` があります |
| `asuka/store` | `store.read(key, default)`、`store.write(key, value)`、`store.remove(key)`：起動をまたいで JSON として保持する小さな値。保存先はプラットフォームの環境設定です（UserDefaults、SharedPreferences、`localStorage`、Windows と Linux ではファイル） |
| `asuka/location` | `location.authorization()`（追跡される）、`request()`、`requestAlways()`、`requestPrecise(purpose)`、`current()`、`positions()`（ストリーム）、`placeName(lat, lon)`、`openSettings()`、`supported()`。バックグラウンドの位置更新（`startUpdates(…)`、`updates()`、`stopUpdates()`、`updating()`）と領域の監視（`monitor(region)`、`crossings()`、`stopMonitoring(id)`、`monitored()`）。そのイベントは読まれるまで保持され、アプリが処理している間は動き続けられます（`hold()`）。Core Location、Android の LocationManager、WinRT の Geolocator、位置情報ポータルまたは GeoClue、Geolocation API |
| `asuka/sensors` | `sensors.available(sensor)`、`accelerometer(every:)`、`gyroscope`、`magnetometer`、`motion`（姿勢、重力、ユーザーの加速度）、`pressure`、`light`、`stepUpdates()`（いずれもストリーム）、`steps(from:, to:)`、`simulate(json)`。Core Motion、SensorManager、Generic Sensor API、iio-sensor-proxy |
| `asuka/purchase` | アプリ内課金：`declare(items)`、`products(items)`、`buy(item, options)`、`Transaction.finish()`、`events()`（アプリの起動からのストリーム）、`owned()`（追跡される：`has(id)`）、`entitlements()`、`restore()`、`statuses(group)`、`manageSubscriptions(id)`、`redeemCode()`、`requestRefund(t)`、`storefront()`。StoreKit 2、Play Billing、Microsoft Store、Digital Goods API |
| `asuka/pay` | Apple Pay と Google Pay：`available(request)`、`pay(request)`（配送先、配送方法、カード、クーポン、承認それぞれに非同期のハンドラーがあり、`Update` を返すか `throw Invalid(field:, message:)` する）、`setUp()`、ウォレット自身の `PayButton` |
| `asuka/appleauth` | Apple でサインイン：`signIn(scopes:, nonce:, state:)`、`credentialState(user)`、`available()`、Apple の `SignInButton`。iOS と macOS のみ |
| `asuka/nfc` | NFC タグ：`state()`（追跡される）、`start(alert:, poll:)` で得る `Session`（`next()`、`setAlert`、`fail`、`stop`）、`Tag`（`message`、`ndef`、`write`、`makeReadOnly`、`transceive`）、`opened()`、NDEF の `Message` と `Record`（`encode()`、`decode(bytes)`）。Core NFC、Android のリーダーモード、Web NFC |
| `asuka/bluetooth` | Bluetooth LE：`state()`（追跡される）、`request()`、`scan()`（ストリーム）、`choose(filters)`、`connect(device)`（`Connection` ストア：`state`、`services()`、`characteristic(s, c)`）、キャラクタリスティックの `read`、`write`、`notifications()`、`serve(services)`、`advertise(…)`、Bluetooth SIG の UUID。Core Bluetooth、Android の BLE、WinRT、BlueZ、Web Bluetooth |

ネイティブパッケージのコードがないプラットフォーム（`asuka test` で動かすアプリにはまったくありません）では、それを必要とする処理は `unsupported` エラーを投げます。そのとき `asuka/store` は何も保存せず、読み込むとデフォルト値が返ります。

デバイスまわりのパッケージ（`location`、`sensors`、`purchase`、`pay`、`appleauth`、`nfc`、`bluetooth`）にはテスト用のフェイクがあります。`location.fake()` や `purchase.fake(products)` などは、プラットフォームの実装の代わりに、テストが操作する Asuka の実装（端末を動かす、購入を承認する、タグをかざす、ペリフェラルを加える）をパッケージに与えます。詳しくは[標準ライブラリの README](../../std/README.md#test-fakes) を参照してください。

## ここにないパッケージ

標準ライブラリにないものは[パッケージ](packages.md)（`asuka add`）から入手します。何もラップしていないプラットフォームの API なら、自分でネイティブパッケージを作ります（[ネイティブコード](native.md)）。
