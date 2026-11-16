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
| `asuka/time` | `time.now()`、`time.date(2026, 3, 7, hour: 9, zone: .utc)`、`time.unix(s)`、`time.parse(rfc3339)`：`Time` を返します。タイムゾーン（`.local`、`.utc`、`.fixed(seconds:)`）付きの時刻で、壁時計としての `year`、`month`、`day`、`hour`、`minute`、`second`、`nanosecond`、`weekday` を持ちます。メソッドは `format("EEE d MMM, HH:mm")`（Unicode の日付パターン、英語で出力）、`rfc3339()`、`add(d)`、`sub(t)`（`Duration` を返す）、`addDate(years:, months:, days:)`、`isBefore`、`isAfter`、`compare`、`sameDay`、`startOfDay`、`truncate(d)`、`inZone`、`utc`、`local`、`unix`、`unixMilli`、`unixNano` です。ほかに `time.monotonic()`（前にしか進まない `Duration`）、`time.since(t)`、`time.ticks(1s)` と `time.ticker(1s)`（時刻のストリーム） |
| `asuka/url` | `url.parse(text)`：`URL?` を返します。`scheme`、`user`、`host`、`port`、`path`、`rawQuery`、`fragment` と、`query(name)`、`queryAll(name)`、`text()`、`resolve(ref)`（RFC 3986）を持ちます。ほかに `url.resolve(base, ref)`、`escape`、`unescape`、`pathEscape`、`pathUnescape`、`encodeQuery(map)`（名前順）、`parseQuery(raw)` |
| `asuka/regex` | `regex.compile(pattern)`（不正なパターンではエラーを投げる）：`Regex` を返し、`matches`、`find`、`findAll`、`findRange`、`findSubmatch`、`findAllSubmatches`、`replace(text, with: "$1")`、`replaceEach(text, f:)`、`split`、`groupNames` を持ちます。`regex.quote(text)` もあります。構文は RE2 で、照合は線形時間です（バックトラックしません） |
| `asuka/encoding` | `base64Encode`、`base64URLEncode`（パディングなし）、`base64Decode`（どちらのアルファベットも可）、`hexEncode`、`hexDecode` |
| `asuka/crypto` | `sha256`、`sha1`、`hmacSHA256(key:, data:)`、`randomBytes(n)`（システムの乱数源）、`uuid()`（バージョン 4）、`equal(a, b)`（定数時間で比較） |
| `asuka/html` | `escape(text)` と `unescape(text)`（数値参照と、テキストでよく使われる名前付き参照） |
| `asuka/random` | `random.int(1, 6)`（両端を含む）、`float()`、`chance(p)`、`pick(items)`、`shuffled(items)`、`seed(n)` |
| `asuka/fs` | `fs.appDir()`（アプリ自身のディレクトリ）、`join([…])`、`clean`、`dir`、`base`、`ext`。ブロックするため `spawn { }` の中で呼ぶもの：`readDir`（`Entry`：`name`、`isDir`、`size`、`modified`）、`stat`、`read`、`readText`、`write`、`writeText`、`append`、`appendText`、`copy`、`rename`、`mkdirAll`、`remove`、`removeAll`、`exists`。失敗すると `FsError`（`.notFound(path)`、`.exists`、`.denied`、`.notDirectory`、`.isDirectory`、`.notEmpty`、`.failed(path, reason)`）を投げます |
| `asuka/apps` | `apps.list()`、`apps.search(apps, query)`（ランチャーと同じ順位付け：名前そのもの、名前の先頭、単語の先頭か各単語の頭文字の並び、どこかに含まれる、文字が順に含まれる。大文字小文字とアクセントは区別しない）、`apps.icon(app, size)`（PNG ファイルのパス）、`apps.open(app)`：macOS、Windows、Linux にインストールされているアプリ。スマートフォンとウェブでは `AppsError.unsupported` を投げます |
| `asuka/dock` | `dock.setProgress(Progress(value:, state:))` と `dock.requestAttention(.critical)`：アプリのアイコンへの進捗表示と注意喚起。macOS の Dock、Windows のタスクバー、Linux のランチャー（デスクトップに Unity のランチャー API があるとき）。スマートフォンとウェブでは何もしません |
| `asuka/hotkey` | `hotkey.Hotkey(hotkey.Shortcut(key:, primary:, shift:, alt:, control:)) { description; on press; on error(e) }`：表示されている間だけ有効なグローバルショートカット（RegisterEventHotKey、RegisterHotKey、GlobalShortcuts ポータル）。スマートフォンとウェブでは `.notAvailable` になります |
| `asuka/i18n` | アプリが表示する言語：`i18n.locale()`、`setLocale(tag)`、`languages()`、`preferredLanguages()`、`isRTL()`、`autonym(tag)`、`region()`。その言語の書き方で数値と日付を書く：`formatNumber`、`formatInt`、`formatPercent`、`formatDate(t, date:, time:)`。アプリのメッセージは `tr.key(…)` で使います（[多言語対応](i18n.md)を参照） |
| `asuka/http` | `http.get`、`http.post`、`http.postJSON`、`http.request`：リクエストはプラットフォーム自身のクライアントを通るので、プロキシ、信頼された証明書、キャッシュ、Cookie はシステムと同じように働きます。`Response` には `status`、`url`、`headers`、`body`、`text()`、`json<T>()`、`ok()` があります |
| `asuka/login` | `login.set(on)`（macOS ではユーザーが許可するまで `LoginError.needsApproval`）、`login.enabled()`、`login.openedAtLogin()`：ユーザーのログイン時にアプリを起動します（SMAppService、Run キー、XDG 自動起動）。スマートフォンのアプリにはできません |
| `asuka/network` | `network.status()`（追跡される：`connected`、`type`、`metered`、`constrained`。読まれている間だけネットワークを監視します）、`network.current()`、`network.changes()`（ストリーム）。iOS と macOS では NWPathMonitor、Android では ConnectivityManager、Windows ではネットワーク リスト マネージャー、Linux では GNetworkMonitor と NetworkManager、ウェブでは `navigator.onLine` と Network Information API |
| `asuka/platform` | `platform.call(module, method, args)` と `platform.send`：ホスト自身のモジュールを名前で呼び出します。引数は各モジュールのリトルエンディアンの形式（`u8`、`u32`、`f64`、`str`、`readU32` など）です。標準ライブラリのパッケージのためのもので、アプリが自分のプラットフォームのコードを呼ぶにはネイティブパッケージ（[ネイティブコード](native.md)）を使います |
| `asuka/recent` | `recent.add(path)`、`recent.clear()`、`recent.list()`：ユーザーが最近開いた書類。「最近使った項目」メニューと Dock メニュー、ジャンプリスト、最近使ったファイルに表示されます |
| `asuka/store` | `store.read(key, default)`、`store.write(key, value)`、`store.remove(key)`、`store.flush()`、`store.dir()`：起動をまたいで保持する小さな値。JSON としてプラットフォームの設定に保存します（UserDefaults、SharedPreferences、`localStorage`、Windows と Linux ではファイル。変更の少し後に書き込みます） |
| `asuka/notify` | ローカル通知：`request()`、`show(n)`、`schedule(n)`（`n.after` の後）、`cancel(id)`、`showing()`、`remove(ids)`、`removeThread(t)`、`removeAll()`、`setBadge(n)`。`setCategories`（アクション、返信）、`setChannels`、`setChannelGroups`（Android）。`Notification(…).from(person, conversation)`（メッセージ）。`present(decide)`（アプリが前面にあるときの表示方法）。`responses()`（タップ、アクション、返信のストリーム。`r.defer()` で作業の間アプリを動かし続けます）。各プラットフォームのホスト自身の通知を使います：UserNotifications、Android の通知、Windows のトースト、Linux の通知、ブラウザーの通知 |
| `asuka/push` | `push.register()`（トークン：APNs、FCM、Web のサブスクリプション）、`push.opened()` と `push.received()`（`Message` のストリーム。`Message.from(person, conversation)`） |
| `asuka/watch` | `watch.path(dir, ignore:, recursive:, attributes:)` は `Watch` を返します：その `root` と、`Event` のまとまりのストリームである `events`（`op`：create、write、remove、rename、attrib、overflow。`path`、`oldPath`）。FSEvents、inotify、ReadDirectoryChangesW（internal/fswatch）を使います。スマートフォンと Web では非対応 |
| `asuka/background` | `background.start(title:, text:, progress:, actions:, onAction:)` は `Task`（`update(…)`、`end()`）を返します：システムが表示する長時間の作業（Android のフォアグラウンドサービスの通知、iOS の継続処理、デスクトップの進捗表示） |
| `asuka/calendar` | `calendar.request()`（`Access`）、`calendars()`、`events(from:, to:, calendars:)`、`add(Event)`（システムのフォーム）：EventKit、Calendar Provider |
| `asuka/compose` | `compose.mail(to:, cc:, bcc:, subject:, body:, html:, attachments:)` と `compose.message(to:, body:, …)` はシステムの作成画面を開き、`Result`（cancelled、sent、saved、unknown）を返します。`check()` |
| `asuka/contacts` | `contacts.pick()`（システムのピッカー：`Contact` または none）、`request()`、`list(name:, limit:)`、`add(Contact)` |
| `asuka/library` | `library.save(path, collection:, name:, folder:, mime:, move:)`：ファイルを写真、ギャラリー、ダウンロード、ミュージックに保存し、保存先を返します |
| `asuka/liveactivity` | `liveactivity.start(Content)` は `Activity`（`update(c, alert:)`、`end(c, dismiss:)`）を返します。`events()`（開始、終了、プッシュトークン）、`enabled()`：iOS のライブアクティビティ、Android 16 のライブアップデート |
| `asuka/periodic` | `periodic.schedule(name, every:, network:, charging:)`、`cancel(name)`、`runs(name)`（`Run` のストリーム。`run.finish(ok)`）：iOS のバックグラウンドタスク、WorkManager |
| `asuka/photos` | 許可を得た写真ライブラリ：`request()`、`current()`、`list(screenshots:, offset:, limit:)`、`thumbnail(id, size:)`、`export(id)`、`manageLimited()` |
| `asuka/widget` | `widget.set(kind, Entry)`、`setAll(kind, entries)`、`clear(kind)`：アプリのウィジェットに表示する内容 |
| `asuka/crash` | `crash.reports()`（ストリーム：保存済みのレポート、その後は届くたびに。`Report`：`id`、`kind`、`fatal`、`time`、`message`、`text`、`threads`、ブレッドクラムと値を含む `context`）、`breadcrumb(message)`、`set(key, value)`、`record(e)`、`delete(id)`。記録はデスクトップのホストが保持します（sdk/crash）。スマートフォンと Web ではまだです（reports は空） |
| `asuka/icloud` | iOS と macOS の iCloud：`account()` と `files(scope)`（追跡されます：iCloud の変化に追従）、`Value(key, default)`（iCloud のキー値ストアに保存されるストア）、`documents()`、`container(id)`、`readFile`、`writeFile`、`delete`、`rename`、`download`、`evict`、`conflicts(path)`、`resolve(path, keeping:)`。ほかのプラットフォームでは、アカウントなし、空のリスト、"icloud: not available" になります |
| `asuka/document` | `document.open(path, read:, write:)` は `Document<T>` ストアを返します：`content`、`state`（loading、saved、edited、saving、conflict、failed）、`error`。`edit(c)`（少し後に自動保存）、`save()`、`saveAnyway()`、`revert()`。iOS と macOS ではファイルアクセスを協調させます。ドキュメントベースのアプリのウィンドウ、メニュー、ドキュメントブラウザー（`Documents<T>`）は、UI ランタイムのウィンドウとナビゲーションを待っています |
| `asuka/host` | 上のパッケージが土台にしているもの：ホスト自身のモジュール（`host.call(module, method, args)`、`host.next(module, method)`）。アプリが直接使う必要はありません |
| `asuka/sqlite` | `sqlite.open(name)`（アプリのデータディレクトリ内のファイル、絶対パス、または `":memory:"`）は `DB` を返します：`migrate(steps)`、`exec(sql, args)` と `query<T>(sql, args)`（非同期、メインスレッド外で実行。行は列名で構造体のフィールドに対応させて `T` に直接デコード）、`watch<T>(sql, args)`（`Live<T>` ストア：`rows`、`loading`、`error`。引数が変わったときと、読んでいるテーブルに書き込みがあったときに再実行）、`changed()`、`close()`。SQLite は sqlite プラグインのライブラリ（sdk/sqlite）で、アプリと一緒に配布されます。Web ではまだ使えません |
| `asuka/storage` | `storage.Value(key, default)`：`value` がデバイスに保存されるストア（asuka/store を使います）。追跡され、変化すると書き込まれます。`reset()`、`storage.flush()`、`storage.dirName()` |
| `asuka/integrity` | `integrity.supported()`、`generateKey()`、`attestKey(id, challenge)`、`assertion(id, data)`（iOS と Mac の App Attest）。`prepare(cloudProjectNumber:)`、`token(requestHash:)`（Play Integrity） |
| `asuka/js` | `js.eval(script, timeout:)`：プラットフォーム自身のエンジン（WKWebView、Android の WebView、WebView2、WebKitGTK、サンドボックス化した frame）で JavaScript を画面外で実行し、その値を JSON で返します。`js.JsError.timeout` |
| `asuka/agerange` | `agerange.request(13, 18)`：生年月日なしでユーザーの年齢層を得ます（iOS 26 の Declared Age Range、Play の Age Signals）。`Range`（`status`、`lower`、`upper`、`source`）を返し、ほかのプラットフォームでは `AgeRangeError.notAvailable` です |
| `asuka/appearance` | `appearance.current()`（追跡されます：`Color?` としての `accent`、`increaseContrast`、`reduceTransparency`。読まれている間だけ設定を監視します）、`appearance.now()`、`appearance.changes()`（ストリーム）。アクセントカラーは Mac、Windows、Linux の設定ポータル、Android の Material You、CSS の AccentColor のものです。iOS にはありません |
| `asuka/biometric` | `biometric.check()`（`kind`、`enrolled`、`passcode`）、`biometric.authenticate(reason:, cancel:, noPasscode:)`：LocalAuthentication、BiometricPrompt、Windows Hello、WebAuthn のプラットフォーム認証器。`BiometricError.canceled`、`.notAvailable`、`.failed` |
| `asuka/device` | `device.info()`（`model`、`system`、`systemVersion`、`appVersion`、`build`）、`device.browse(url)`（アプリ内で：SFSafariViewController、Custom Tab）、`device.pickPhotos(max:)`、`device.pickFiles(types:, multiple:)`（システムのピッカー。許可は不要で、コピーはアプリのキャッシュに置かれます）、`device.clipboardText()`、`clipboardHas()`、`clipboardImage()`、`copyImage(path)`、`clipboardChanges()`（ストリーム）、`device.keepAwake(on)` |
| `asuka/dialog` | `dialog.open(...)`、`dialog.save(name:, ...)`、`dialog.chooseFolder(...)`、`chooseFolders`：システムのファイルダイアログ（NSOpenPanel と NSSavePanel、コモンアイテムダイアログ、GtkFileDialog、File System Access のピッカー）で、ユーザー自身のパスを返します。`dialog.Filter(name:, types:, extensions:)`。スマートフォンでは open はドキュメントピッカーです |
| `asuka/power` | `power.status()`（追跡されます：`battery`、`onBattery`、`level`、`charging`、`lowPower`、`thermal`）、`power.events()`、`suspends()`、`resumes()`、`locks()`、`unlocks()`（ストリーム）、`power.idleTime()`：IOKit と ProcessInfo、UIDevice、BatteryManager と PowerManager、GetSystemPowerStatus とセッションの通知、UPower と logind、Battery Status API |
| `asuka/preview` | `preview.show(sources, start:, edit:)`：ファイル（パス、アセット、URL）をシステムのビューアーで表示します（iOS と Mac ではクイックルック、ほかではファイルに対応するアプリ、Web では新しいタブ）。`preview.PreviewError.noViewer` |
| `asuka/print` | `print.file(source, job:, orientation:)`、`print.html(markup, ...)`：システムの印刷ダイアログで、`print.Outcome` を返します。`print.pdf(markup, page:, landscape:, margins:)`：マークアップの PDF を作り、そのパスを返します。`print.Page.a4`、`letter`…、`print.regionPage(locale)`。名前が prelude の `print` と同じなので、`import asuka/print as printing` と書きます |
| `asuka/review` | `review.request()`：ストアの評価プロンプト（iOS と Mac の StoreKit、Play のアプリ内レビュー）。表示するかどうかはストアの割り当て次第です。Windows、Linux、Web では何もしません。「評価する」ボタンには `review.appStoreURL(id)`、`review.playStoreURL(id)` |
| `asuka/screen` | `screen.displays()`（追跡されます：`id`、`name`、`bounds`、`workArea`、`scale`、`refreshRate`、`primary`）、`screen.primary`、`nearest`、`matching`。`screen.lock(orientation)`、`screen.setBars(style:, hideStatus:, hideNavigation:)`、`screen.preventCapture(on)`、`screen.screenshots()`（ストリーム） |
| `asuka/secure` | `secure.read(key)`、`secure.write(key, value)`、`secure.remove(key)`：プラットフォームが暗号化する小さな秘密情報（キーチェーン、Android Keystore、資格情報マネージャー、libsecret を通したユーザーのキーリング、抽出不可の WebCrypto 鍵で暗号化した IndexedDB） |
| `asuka/shell` | `shell.reveal(path)`、`shell.open(path)`、`shell.trash(path)`、`shell.beep()`：ファイルマネージャーの機能（NSWorkspace、エクスプローラーとごみ箱、GIO と FileManager1）。`shell.ShellError.noApp` |
| `asuka/splash` | `splash.hold()`、`splash.hide()`：アプリの準備ができるまで asuka.toml の起動画面を表示し続けます（iOS、Android。ほかでは何もしません） |
| `asuka/tracking` | `tracking.request()`、`tracking.current()`：iOS と Mac の App Tracking Transparency のプロンプト（`.notAsked`、`.restricted`、`.denied`、`.allowed`）、Windows の広告 ID の設定、ブラウザーの Global Privacy Control または Do Not Track。プロンプトのない Android と Linux では `.allowed` |
| `asuka/webauth` | `webauth.open(url, callbackScheme:, ephemeral:)`：システムの画面でのサインイン（ASWebAuthenticationSession、Custom Tab、WebView2 と WebKitGTK のウィンドウ、ポップアップ）で、コールバック URL を返します。`WebAuthError.canceled`。タスクをキャンセルすると閉じます |
| `asuka/location` | `location.authorization()`（追跡される）、`request()`、`requestAlways()`、`requestPrecise(purpose)`、`current()`、`positions()`（ストリーム）、`placeName(lat, lon)`、`openSettings()`、`supported()`。バックグラウンドの位置更新（`startUpdates(…)`、`updates()`、`stopUpdates()`、`updating()`）と領域の監視（`monitor(region)`、`crossings()`、`stopMonitoring(id)`、`monitored()`）。そのイベントは読まれるまで保持され、アプリが処理している間は動き続けられます（`hold()`）。Core Location、Android の LocationManager、WinRT の Geolocator、位置情報ポータルまたは GeoClue、Geolocation API |
| `asuka/sensors` | `sensors.available(sensor)`、`accelerometer(every:)`、`gyroscope`、`magnetometer`、`motion`（姿勢、重力、ユーザーの加速度）、`pressure`、`light`、`stepUpdates()`（いずれもストリーム）、`steps(from:, to:)`、`simulate(json)`。Core Motion、SensorManager、Generic Sensor API、iio-sensor-proxy |
| `asuka/purchase` | アプリ内課金：`declare(items)`、`products(items)`、`buy(item, options)`、`Transaction.finish()`、`events()`（アプリの起動からのストリーム）、`owned()`（追跡される：`has(id)`）、`entitlements()`、`restore()`、`statuses(group)`、`manageSubscriptions(id)`、`redeemCode()`、`requestRefund(t)`、`storefront()`。StoreKit 2、Play Billing、Microsoft Store、Digital Goods API |
| `asuka/pay` | Apple Pay と Google Pay：`available(request)`、`pay(request)`（配送先、配送方法、カード、クーポン、承認それぞれに非同期のハンドラーがあり、`Update` を返すか `throw Invalid(field:, message:)` する）、`setUp()`、ウォレット自身の `PayButton` |
| `asuka/appleauth` | Apple でサインイン：`signIn(scopes:, nonce:, state:)`、`credentialState(user)`、`available()`、Apple の `SignInButton`。iOS と macOS のみ |
| `asuka/nfc` | NFC タグ：`state()`（追跡される）、`start(alert:, poll:)` で得る `Session`（`next()`、`setAlert`、`fail`、`stop`）、`Tag`（`message`、`ndef`、`write`、`makeReadOnly`、`transceive`）、`opened()`、NDEF の `Message` と `Record`（`encode()`、`decode(bytes)`）。Core NFC、Android のリーダーモード、Web NFC |
| `asuka/bluetooth` | Bluetooth LE：`state()`（追跡される）、`request()`、`scan()`（ストリーム）、`choose(filters)`、`connect(device)`（`Connection` ストア：`state`、`services()`、`characteristic(s, c)`）、キャラクタリスティックの `read`、`write`、`notifications()`、`serve(services)`、`advertise(…)`、Bluetooth SIG の UUID。Core Bluetooth、Android の BLE、WinRT、BlueZ、Web Bluetooth |

ネイティブパッケージのコードがないプラットフォーム（`asuka test` で動かすアプリにはまったくありません）では、それを必要とする処理は `unsupported` エラーを投げます。そのとき `asuka/store` は何も保存せず、読み込むとデフォルト値が返ります。

`asuka test` では実行のたびに同じ結果になります。`time.now()` は 2026-01-01T00:00:00Z から始まり、テストの時間（`sleep`）に合わせてだけ進みます。ローカルのタイムゾーンは UTC で、各テストには `fs.appDir()` として専用の空のディレクトリがあります。Web ではアプリにまだディレクトリがないので、`asuka/fs` の呼び出しはエラーを投げます。

デバイスまわりのパッケージ（`location`、`sensors`、`purchase`、`pay`、`appleauth`、`nfc`、`bluetooth`）にはテスト用のフェイクがあります。`location.fake()` や `purchase.fake(products)` などは、プラットフォームの実装の代わりに、テストが操作する Asuka の実装（端末を動かす、購入を承認する、タグをかざす、ペリフェラルを加える）をパッケージに与えます。詳しくは[標準ライブラリの README](../../std/README.md#test-fakes) を参照してください。

## ここにないパッケージ

標準ライブラリにないものは[パッケージ](packages.md)（`asuka add`）から入手します。何もラップしていないプラットフォームの API なら、自分でネイティブパッケージを作ります（[ネイティブコード](native.md)）。
