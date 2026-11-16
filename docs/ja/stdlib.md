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
| `asuka/i18n` | アプリが表示する言語：`i18n.locale()`、`setLocale(tag)`、`languages()`、`preferredLanguages()`、`isRTL()`、`autonym(tag)`、`region()`。その言語の書き方で数値と日付を書く：`formatNumber`、`formatInt`、`formatPercent`、`formatDate(t, date:, time:)`。アプリのメッセージは `tr.key(…)` で使います（[多言語対応](i18n.md)を参照） |
| `asuka/http` | `http.get`、`http.post`、`http.postJSON`、`http.request`：リクエストはプラットフォーム自身のクライアントを通るので、プロキシ、信頼された証明書、キャッシュ、Cookie はシステムと同じように働きます。`Response` には `status`、`url`、`headers`、`body`、`text()`、`json<T>()`、`ok()` があります |
| `asuka/store` | `store.read(key, default)`、`store.write(key, value)`、`store.remove(key)`：起動をまたいで JSON として保持する小さな値。保存先はプラットフォームの環境設定です（UserDefaults、SharedPreferences、`localStorage`、Windows と Linux ではファイル） |

ネイティブパッケージのコードがないプラットフォーム（`asuka test` で動かすアプリにはまったくありません）では、それを必要とする処理は `unsupported` エラーを投げます。そのとき `asuka/store` は何も保存せず、読み込むとデフォルト値が返ります。

## ここにないパッケージ

標準ライブラリにないものは[パッケージ](packages.md)（`asuka add`）から入手します。何もラップしていないプラットフォームの API なら、自分でネイティブパッケージを作ります（[ネイティブコード](native.md)）。
