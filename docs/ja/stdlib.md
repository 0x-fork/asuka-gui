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
| `asuka/store` | `store.read(key, default)`、`store.write(key, value)`、`store.remove(key)`、`store.flush()`、`store.dir()`：起動をまたいで JSON として保持する小さな値。保存先はプラットフォームの環境設定です（UserDefaults、SharedPreferences、`localStorage`、Windows と Linux ではファイルで、変更の少し後に書き込まれます） |
| `asuka/notify` | ローカル通知：`request()`、`show(n)`、`schedule(n)`（`n.after` の後に表示）、`cancel(id)`、`showing()`、`remove(ids)`、`removeThread(t)`、`removeAll()`、`setBadge(n)`。`setCategories`（アクション、返信）、`setChannels`、`setChannelGroups`（Android）。`Notification(…).from(person, conversation)`（メッセージ）。`present(decide)`（アプリが前面にあるときの表示方法）。`responses()`（タップ、アクション、返信のストリーム。`r.defer()` で処理が終わるまでアプリを動かし続けます）。どのプラットフォームでもホスト自身の通知を使います：UserNotifications、Android の通知、Windows のトースト、Linux の通知、ブラウザーの通知 |
| `asuka/push` | `push.register()`（トークン：APNs のもの、FCM のもの、Web の購読）、`push.opened()` と `push.received()`（`Message` のストリーム。`Message.from(person, conversation)`） |
| `asuka/watch` | `watch.path(dir, ignore:, recursive:, attributes:)` は `Watch` を返します。`root` と、`Event` のまとまりのストリームである `events`（`op`：create、write、remove、rename、attrib、overflow。`path`、`oldPath`）を持ちます。FSEvents、inotify、ReadDirectoryChangesW（internal/fswatch）を使います。スマートフォンと Web では使えません |
| `asuka/background` | `background.start(title:, text:, progress:, actions:, onAction:)` は `Task`（`update(…)`、`end()`）を返します：システムが表示する長い処理（Android のフォアグラウンドサービスの通知、iOS の継続処理、デスクトップの進捗表示） |
| `asuka/calendar` | `calendar.request()`（`Access`）、`calendars()`、`events(from:, to:, calendars:)`、`add(Event)`（システムのフォーム）：EventKit、Calendar Provider |
| `asuka/compose` | `compose.mail(to:, cc:, bcc:, subject:, body:, html:, attachments:)` と `compose.message(to:, body:, …)` はシステムの作成画面を表示し、`Result`（cancelled、sent、saved、unknown）を返します。`check()` |
| `asuka/contacts` | `contacts.pick()`（システムのピッカー：`Contact` か none）、`request()`、`list(name:, limit:)`、`add(Contact)` |
| `asuka/library` | `library.save(path, collection:, name:, folder:, mime:, move:)`：ファイルを写真、ギャラリー、ダウンロード、ミュージックに保存し、保存先を返します |
| `asuka/liveactivity` | `liveactivity.start(Content)` は `Activity`（`update(c, alert:)`、`end(c, dismiss:)`）を返します。`events()`（開始、終了、プッシュトークン）、`enabled()`：iOS のライブアクティビティ、Android 16 の Live Updates |
| `asuka/periodic` | `periodic.schedule(name, every:, network:, charging:)`、`cancel(name)`、`runs(name)`（`Run` のストリーム、`run.finish(ok)`）：iOS のバックグラウンドタスク、WorkManager |
| `asuka/photos` | 許可を得て写真ライブラリを扱います：`request()`、`current()`、`list(screenshots:, offset:, limit:)`、`thumbnail(id, size:)`、`export(id)`、`manageLimited()` |
| `asuka/widget` | `widget.set(kind, Entry)`、`setAll(kind, entries)`、`clear(kind)`：アプリのウィジェットに表示する内容 |
| `asuka/crash` | `crash.reports()`（ストリーム：保持されているレポート、その後は届くたびに一つずつ。`Report`：`id`、`kind`、`fatal`、`time`、`message`、`text`、`threads`、ブレッドクラムと値を含む `context`）、`breadcrumb(message)`、`set(key, value)`、`record(e)`、`delete(id)`。実行の記録は、デスクトップではホストが（sdk/crash）、iOS、Android、Web ではコアが保持します（コア自身の致命的なエラー、捕捉されなかった例外、iOS の MetricKit の診断、Android のプロセスの終了理由、捕捉されなかった JavaScript のエラー） |
| `asuka/icloud` | iOS と macOS の iCloud：`account()` と `files(scope)`（追跡され、iCloud が変更すると更新されます）、`Value(key, default)`（iCloud のキーバリューストアに保持されるストア）、`documents()`、`container(id)`、`readFile`、`writeFile`、`delete`、`rename`、`download`、`evict`、`conflicts(path)`、`resolve(path, keeping:)`。ほかのプラットフォームでは、アカウントなし、空のリスト、「icloud: not available」になります |
| `asuka/document` | `document.open(path, read:, write:)` は `Document<T>` ストアを返します：`content`、`state`（loading、saved、edited、saving、conflict、failed）、`error`。`edit(c)`（少し後に自動保存）、`save()`、`saveAnyway()`、`revert()`。iOS と macOS ではファイルを協調して扱います。ドキュメントベースのアプリのウィンドウ、メニュー、ドキュメントブラウザー（`Documents<T>`）は、UI ランタイムのウィンドウとナビゲーションを待っています |
| `asuka/host` | 上のパッケージが土台にしているもの：ホスト自身のモジュール（`host.call(module, method, args)`、`host.next(module, method)`）。アプリでは使いません |
| `asuka/sqlite` | `sqlite.open(name)`（アプリのデータディレクトリ内のファイル、絶対パス、または `":memory:"`）は `DB` を返します：`migrate(steps)`、`exec(sql, args)` と `query<T>(sql, args)`（非同期で、メインスレッドの外で実行。行は `T` に直接デコードされ、構造体のフィールドは列名で対応します）、`watch<T>(sql, args)`（`Live<T>` ストア：`rows`、`loading`、`error`。引数が変わったとき、読み取るテーブルに書き込みがあったときに再実行されます）、`changed()`、`close()`。SQLite は sqlite プラグインのライブラリ（sdk/sqlite）で、アプリに同梱されます。Web ではページのワーカーで動きます |
| `asuka/storage` | `storage.Value(key, default)`：`value` がデバイスに保持されるストア（asuka/store を使います）。追跡され、変わると書き込まれます。`reset()`、`storage.flush()`、`storage.dirName()` |

ネイティブパッケージのコードがないプラットフォーム（`asuka test` で動かすアプリにはまったくありません）では、それを必要とする処理は `unsupported` エラーを投げます。そのとき `asuka/store` は何も保存せず、読み込むとデフォルト値が返ります。

`asuka test` は実行結果を毎回同じにします。`time.now()` は 2026-01-01T00:00:00Z から始まり、テストの時間（`sleep`）が進んだときだけ進みます。ローカルのタイムゾーンは UTC で、各テストには `fs.appDir()` として専用の空のディレクトリが用意されます。Web では、アプリのディレクトリはページのもので、ブラウザーのストレージに保持されます（[アプリのファイル](web.md#アプリのファイル)）。

## ここにないパッケージ

標準ライブラリにないものは[パッケージ](packages.md)（`asuka add`）から入手します。何もラップしていないプラットフォームの API なら、自分でネイティブパッケージを作ります（[ネイティブコード](native.md)）。
