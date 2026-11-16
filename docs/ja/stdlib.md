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
| `asuka/time` | `time.now()`（1970 年からのミリ秒）、`time.monotonic()`（前にしか進まない `Duration`） |
| `asuka/apps` | `apps.list()`、`apps.search(apps, query)`（ランチャーと同じ順位付け：名前そのもの、名前の先頭、単語の先頭か各単語の頭文字の並び、どこかに含まれる、文字が順に含まれる。大文字小文字とアクセントは区別しない）、`apps.icon(app, size)`（PNG ファイルのパス）、`apps.open(app)`：macOS、Windows、Linux にインストールされているアプリ。スマートフォンとウェブでは `AppsError.unsupported` を投げます |
| `asuka/dock` | `dock.setProgress(Progress(value:, state:))` と `dock.requestAttention(.critical)`：アプリのアイコンへの進捗表示と注意喚起。macOS の Dock、Windows のタスクバー、Linux のランチャー（デスクトップに Unity のランチャー API があるとき）。スマートフォンとウェブでは何もしません |
| `asuka/hotkey` | `hotkey.Hotkey(hotkey.Shortcut(key:, primary:, shift:, alt:, control:)) { description; on press; on error(e) }`：表示されている間だけ有効なグローバルショートカット（RegisterEventHotKey、RegisterHotKey、GlobalShortcuts ポータル）。スマートフォンとウェブでは `.notAvailable` になります |
| `asuka/http` | `http.get`、`http.post`、`http.postJSON`、`http.request`：リクエストはプラットフォーム自身のクライアントを通るので、プロキシ、信頼された証明書、キャッシュ、Cookie はシステムと同じように働きます。`Response` には `status`、`url`、`headers`、`body`、`text()`、`json<T>()`、`ok()` があります |
| `asuka/login` | `login.set(on)`（macOS ではユーザーが許可するまで `LoginError.needsApproval`）、`login.enabled()`、`login.openedAtLogin()`：ユーザーのログイン時にアプリを起動します（SMAppService、Run キー、XDG 自動起動）。スマートフォンのアプリにはできません |
| `asuka/network` | `network.status()`（追跡される：`connected`、`type`、`metered`、`constrained`。読まれている間だけネットワークを監視します）、`network.current()`、`network.changes()`（ストリーム）。iOS と macOS では NWPathMonitor、Android では ConnectivityManager、Windows ではネットワーク リスト マネージャー、Linux では GNetworkMonitor と NetworkManager、ウェブでは `navigator.onLine` と Network Information API |
| `asuka/platform` | `platform.call(module, method, args)` と `platform.send`：ホスト自身のモジュールを名前で呼び出します。引数は各モジュールのリトルエンディアンの形式（`u8`、`u32`、`f64`、`str`、`readU32` など）です。標準ライブラリのパッケージのためのもので、アプリが自分のプラットフォームのコードを呼ぶにはネイティブパッケージ（[ネイティブコード](native.md)）を使います |
| `asuka/recent` | `recent.add(path)`、`recent.clear()`、`recent.list()`：ユーザーが最近開いた書類。「最近使った項目」メニューと Dock メニュー、ジャンプリスト、最近使ったファイルに表示されます |
| `asuka/store` | `store.read(key, default)`、`store.write(key, value)`、`store.remove(key)`：起動をまたいで JSON として保持する小さな値。保存先はプラットフォームの環境設定です（UserDefaults、SharedPreferences、`localStorage`、Windows と Linux ではファイル） |

ネイティブパッケージのコードがないプラットフォーム（`asuka test` で動かすアプリにはまったくありません）では、それを必要とする処理は `unsupported` エラーを投げます。そのとき `asuka/store` は何も保存せず、読み込むとデフォルト値が返ります。

## ここにないパッケージ

標準ライブラリにないものは[パッケージ](packages.md)（`asuka add`）から入手します。何もラップしていないプラットフォームの API なら、自分でネイティブパッケージを作ります（[ネイティブコード](native.md)）。
