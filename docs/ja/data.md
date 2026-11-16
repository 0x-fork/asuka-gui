# データ

アプリはデータをいくつかの場所に保存します。小さな値は `asuka/storage` に、レコードは `asuka/sqlite` で SQLite に、秘密情報は `asuka/secure` でプラットフォームのキーチェーンに、ユーザーのデバイス間で共有する値は `asuka/icloud` で iCloud に、ドキュメントのファイルは `asuka/document` に置きます。このページではそれぞれを説明し、`asuka/http` によるネットワーク、`asuka/watch` によるファイルの監視、そしてどの処理がメインスレッドの外で行われるかも扱います。

## データの保存場所

各パッケージは、そのプラットフォーム自身のアプリがデータを置く場所に保存します。

| | `asuka/storage`（と `asuka/store`） | `asuka/sqlite` で名前だけを指定して開いたデータベース |
|---|---|---|
| iOS | UserDefaults | アプリのコンテナー内の Application Support |
| macOS | UserDefaults | `~/Library/Application Support/<bundle ID>` |
| Android | SharedPreferences | アプリの files ディレクトリ |
| Windows | `%LOCALAPPDATA%\<name>` 内の `store.json` | `%LOCALAPPDATA%\<name>` |
| Linux | `$XDG_DATA_HOME/<id>` 内の `store.json` | `$XDG_DATA_HOME/<id>`（`~/.local/share/<id>`。Flatpak ではサンドボックス自身のもの） |
| Web | `localStorage` | ブラウザーの Origin Private File System（ない場合は IndexedDB） |

`<name>` は `asuka.toml` の `name`、`<id>` は `id` です。アプリが動いているプラットフォームで値がどこにあるかは `storage.dirName()` で分かります。

`asuka dev` では、デスクトップアプリのデータディレクトリは `asuka dev` 専用のもの（ユーザーのキャッシュディレクトリ内、アプリごとに一つ）になります。そのため、開発中に試したことがインストール済みのビルドのデータと混ざることはありません。

## 小さな値：asuka/storage

`asuka/storage` は、起動をまたいで小さな値を保存します。設定、サインインしているユーザーの名前、最後に開いていたタブなどです。`storage.Value` は、`value` が保存される[ストア](language.md#ストア)です。最初は保存されている値（初回はデフォルト値）で始まり、代入した値はそのたびに保存されます。ほかの状態と同じく、読み取りは追跡され、書き込みはすぐ画面に反映されます。

```asuka
import asuka/storage

let wifiOnly = storage.Value("wifiOnly", false)

component DownloadSettings {
  Column {
    Toggle { bind on: wifiOnly.value; title: "Download on Wi-Fi only" }
    if wifiOnly.value { Text("Large files wait for Wi-Fi") { color: .secondaryLabel } }
  }
}
```

値には JSON で表せるものなら何でも使えます。数値、文字列、配列、構造体、列挙型です。デフォルト値から型が分からないときは、型を書きます。

```asuka
struct Account {
  id: string
  email: string
}

let accounts: storage.Value<[Account]> = storage.Value("accounts", [])

fn signIn(a: Account) {
  accounts.value.push(a)
}
```

保存先はプラットフォームの設定の仕組みです。iOS と macOS では UserDefaults、Android では SharedPreferences、Web では `localStorage`、Windows と Linux では `store.json` というファイルで、変更の少し後に書き込まれます。`storage.flush()` はすぐに書き込みます。アプリが終了されるかもしれない前（バックグラウンドタスクの最後のステップなど）に呼びます。`value.reset()` は保存された値を忘れます。保存された値はストアが作られるときに読み込まれ、もうその型としてデコードできない場合（型が変わった場合）はデフォルト値になります。

`asuka/store` は同じことをストアなしで行います。`store.read(key, default)`、`store.write(key, value)`、`store.remove(key)` で、自分で決めたタイミングで値を保存するコード向けです。

これはデータベースではありません。レコードが多い、または大きいときは SQLite を使います。

## レコード：asuka/sqlite

`asuka/sqlite` はアプリの SQLite データベースです。SQLite そのものであり、同じリリースをすべてのプラットフォームで同じ方法でビルドしたもので、sqlite [プラグイン](plugins.md)から提供されます。パッケージをインポートするだけで、CLI がプラグインのライブラリをアプリと一緒に配布します。コアはステートメントを一つずつ丸ごとメインスレッドの外で実行し、返ってきた行を求められた型に直接デコードします。[`examples/notes`](../../examples/notes) がこれを使っています。

### データベースを開いてマイグレーションする

```asuka
import asuka/sqlite

struct Note {
  id: int
  text: string
  done: bool
}

fn openDB() throws -> sqlite.DB {
  let db = try sqlite.open("notes.db")
  try db.migrate([
    // 1
    "create table notes (id integer primary key, text text not null, done bool not null default 0)",
    // 2
    "alter table notes add column created integer not null default 0",
  ])
  return db
}

let db = (try? openDB())!
```

`sqlite.open(name)` はデータベースを開きます（なければ作ります）。名前だけならアプリのデータディレクトリ内のファイル（[データの保存場所](#データの保存場所)を参照）、絶対パスならそのファイル、`":memory:"` ならメモリ上のデータベースです。ファイルのデータベースは先行書き込みログ（WAL）で開かれ、外部キー制約が有効になります。

`db.migrate(steps)` はスキーマを最新にします。steps はデータベースのバージョンを順に並べたもので、各ステップの SQL（セミコロンで区切った複数の文）は一度だけ、トランザクションの中で実行され、その番号が `PRAGMA user_version` に記録されます。バージョン 1 のデータベースはステップ 2 以降を、新しいデータベースはすべてを実行します。ステップは末尾に追加し、リリース済みのステップは変更しないでください。メインスレッドで実行されるので、アプリの起動時に呼びます。Web では、ページのメインスレッドが SQLite のワーカーを待てないため、`open` と `migrate` は応答を待たずにワーカーへ送られ、その後のステートメントより先に実行されます。開けなかったデータベースでは以降のステートメントがすべてその理由とともに失敗し、マイグレーションが失敗すると次のステートメントがそのエラーで失敗します。

### クエリ

```asuka
async fn undone() throws -> [Note] = try await db.query("select * from notes where done = ? order by id", [false])

async fn noteCount() throws -> int {
  let n: [int] = try await db.query("select count(*) from notes", [])
  return n.first() ?? 0
}
```

`db.query<T>(sql, args)` はクエリをメインスレッドの外で実行し、結果の行を `T` として返します。構造体のフィールドは列名で埋められます（`select *` なら `Note` が埋まります）。対応する列のないフィールドはゼロ値（`0`、`""`、`false`、`[]`、`none`）になり、対応するフィールドのない列は無視されます。それ以外の `T` は最初の列から作られます。件数なら `[int]`、名前なら `[string]` です。

列はできるかぎりフィールドの型に変換されます。整数は `int`、`float`、`bool`（0 以外が真）、`string` に、実数は `float` か `int` に、テキストは `string`、`bytes`、それが表す数値、case 名で対応するペイロードなしの列挙型、そして JSON なら構造体・配列・マップに、blob は `bytes` か JSON に変換されます。null はフィールドのゼロ値（オプショナルなら `none`）です。変換できない列があると、その列名を示してクエリが失敗します。

### 引数

ステートメントの引数は、タプルか配列なら位置（`?`、`?1`）で、構造体か `[string: T]` なら名前（`:name`、`@name`、`$name`）と位置で対応します。

```asuka
async fn add(text: string) throws {
  try await db.exec("insert into notes (text, created) values (?, ?)", (text, 1700000000))
}

struct Rename {
  id: int
  text: string
}

async fn rename(r: Rename) throws {
  try await db.exec("update notes set text = :text where id = :id", r)
}
```

それぞれは `int`、`float`、`bool`（0 か 1 として保存）、`string`、`bytes`（blob）、ペイロードなしの列挙型の case（その番号）、またはそれらのオプショナル（`none` は null）です。`[]` は引数なしを表します。

### 実行

`db.exec(sql, args)` は一つの文（insert、update、delete、create など）、またはセミコロンで区切った複数の文を、メインスレッドの外で、それより前の文の後に実行します。`exec` の後に続くクエリは、それが書き込んだ内容を見ることができます。トランザクションは SQL で書きます。一回の `exec` の中に `begin`、各文、`commit` を並べます。

### ライブクエリ

`db.watch<T>(sql, args)` はライブクエリです。結果の行を持つストアで、いまメインスレッドの外で一度取得し、その後このデータベースを通じて、それが読むテーブルへの書き込みがコミットされるたびに取得し直します。コンポーネントの中で作ると、コンポーネントと一緒に止まります。

```asuka
let search = "select * from notes where ?1 = '' or instr(lower(text), lower(?1)) > 0 order by done, id desc"

component Notes {
  state query = ""
  let notes: sqlite.Live<Note> = db.watch(search, [query])

  Column {
    TextField { bind text: query; placeholder: "Search" }
    if notes.loading { Text("Loading…") }
    if let e = notes.error { Text(e) { color: .red } }
    for n in notes.rows key n.id { Text(n.text) }
  }
}
```

`Live<T>` には、最新の行である `rows`、最初の取得が終わるまで true の `loading`、前回の取得が失敗した理由である `error`（成功すると none）があります。これらは状態なので、それを表示している部分は変化に追従します。

ライブクエリは、それが読むテーブルのためだけに再実行されます。読むテーブルは SQLite がステートメントを準備するときに分かり、ビューの背後にあるテーブルも含まれます。そのいずれかへの書き込みがコミットされると再実行され、トリガーや外部キーのアクションが書いた行も対象です。ほかのテーブルへの書き込みでは再実行されません。

引数は `[query]` のように状態を読んでもかまいません。引数が変わると `let` がライブクエリを作り直します。新しいライブクエリは、新しい行が届くまで同じ SQL の前回の行から始まるので、入力中にリストが空になることはありません。

`db.changed()` はすべてのライブクエリを再実行します。データベース自身には見えない変更（別プロセスによる変更、ファイルの差し替え）のときに使います。

### エラー

SQLite が返すエラーのメッセージは SQLite のもので、結果コードが付きます。`sqlite: UNIQUE constraint failed: users.email (2067)` のようになります。

```asuka
async fn signUp(email: string) -> string {
  do {
    try await db.exec("insert into users (email) values (?)", [email])
    return ""
  } catch e {
    if e.message.contains("UNIQUE constraint failed") { return "That email is taken" }
    return e.message
  }
}
```

### ライブラリ

SQLite はすべてのプラットフォームでリリース 3.54.1 で、FTS5、R*Tree、Geopoly、数学関数、`dbstat`、`STAT4`、`soundex` を組み込み、読み込み可能な拡張は含めず、一時テーブルはメモリに置きます。sqlite プラグインのライブラリ `asqlite`（[プラグイン](plugins.md)）で、`asuka build` がコアの見つけられる場所に配置します。Mac の `Contents/Frameworks`、Windows アプリの `.exe` の隣、Linux のインストール先の `lib/<app>`、iOS アプリの `Frameworks` 内のフレームワーク、APK の各 ABI の `libasuka.so` の隣です。デスクトップでの `asuka dev`、`asuka test`、`asuka run` は、プラグインのビルド済みファイルか公開済みファイル（`ASUKA_LIB_ASQLITE`）をコアに使わせます。後者は一度だけダウンロードされ、ユーザーのキャッシュに保存されます。Web では同じライブラリを WebAssembly にビルドしたものが、ページのワーカーで動きます（[Web の SQLite](web.md#sqlite)）。

## 秘密情報：asuka/secure

`asuka/secure` は、トークンやパスワードのような小さな秘密情報を、プラットフォームに暗号化させて保存します。秘密情報のためのもので、データのためのものではありません。それ以外はストアかデータベースに置いてください。

- **iOS**：キーチェーン。アプリの bundle ID で汎用パスワードとして保存し、デバイスの最初のロック解除の後に読めるようになり、このデバイスだけに残ります。
- **Android**：各値を Android Keystore 内の鍵で暗号化し（AES-GCM）、アプリ専用の設定に保存します。鍵が Keystore の外に出ることはありません。
- **macOS**：ログインキーチェーン。アプリの bundle ID（なければアプリ名）で汎用パスワードとして保存し、キーチェーンアクセスで確認できます。アドホック署名のアプリでも使えます。`asuka dev` のアプリや Developer ID のないビルドがそうです。
- **Windows**：資格情報マネージャー。ユーザーの汎用資格情報として `<name>/<key>` という名前で、このコンピューターだけに保存します（ローミングしません）。コントロールパネルの資格情報マネージャーで確認できます。値は 2560 バイトまでです。
- **Linux**：ユーザーのキーリング。libsecret と Secret Service（GNOME Keyring、KWallet、KeePassXC）を通じて、キーごとにアプリ独自のスキーマ（その ID）の項目を、`<name>: <key>` というラベルでデフォルトのコレクションに保存します。キーリングのアプリ（GNOME の「パスワードと鍵」）で確認できます。libsecret がないか、キーリングが動いていなければ、呼び出しはその旨を伝えて失敗します。
- **Web**：ブラウザーにはキーチェーンがありません。各値を WebCrypto が抽出不可として作った鍵で暗号化し（AES-GCM、256 ビット）、鍵と一緒にアプリの IndexedDB データベースに保存します。そのため、どのスクリプトも鍵のバイト列を読めず、値が平文でディスクに置かれることもありません。ただし、アプリのオリジンで動くコードなら鍵に復号させることはできます。Web アプリとしての最善策であって、キーチェーンではありません。https か localhost が必要です。

`secure.read(key)` はキーの値を返し（なければ `none`）、`secure.write(key, value)` は値を保存し、`secure.remove(key)` は削除します。どれもメインスレッドを止めずにプラットフォームを待ち、プラットフォームが返したエラー（ロックされたキーリング、キーリングがない）をそのまま投げます。プラットフォームにそのためのコードがない場合（`asuka test`）は `unsupported` エラーを投げます。

```asuka
import asuka/secure

component SignedIn {
  state status = ""

  task {
    do {
      status = if (try await secure.read("token")) != none { "Signed in" } else { "Signed out" }
    } catch e {
      status = e.message
    }
  }

  Column {
    Text(status)
    Button("Sign out") {
      on press {
        try? await secure.remove("token")
        status = "Signed out"
      }
    }
  }
}

async fn keepToken(token: string) throws {
  try await secure.write("token", token)
}
```

## ユーザーのデバイス間で共有する値：asuka/icloud

iOS と macOS では、`icloud.Value(key, default)` は iCloud のキーバリューストアに保存されるストアで、別のデバイスが値を変えると読み直されます。`storage.Value` がデバイスに保存されるのと同じ感覚です。

```asuka
import asuka/icloud

let textSize = icloud.Value("textSize", 17.0)
```

`icloud.account()` と `icloud.files(scope)` はアカウントと iCloud 内のアプリのファイルを追跡し、`readFile`、`writeFile`、`conflicts`、`resolve` でそれらを扱います。[iCloud](icloud.md) を参照してください。ほかのプラットフォームでは、値はデフォルト値と、デバイス上で設定された値のままです。

## ドキュメントのファイル：asuka/document

`document.open(path, read:, write:)` は、ファイルの内容をストアにします。アプリの `read` でメインスレッドの外で読み込み、`edit` のたびに少し後でアプリの `write` を使って保存し、元に戻すこともできます。読み込んだ後に別のプログラムがファイルを変更していれば、競合になります。

```asuka
import asuka/document

component Editor(path: string) {
  let doc = document.open(path, read: data => data.string(), write: text => text.bytes())

  Column {
    if let text = doc.content {
      TextField { text: text; multiline: true; on change(t) { doc.edit(t) } }
    }
    if doc.state == .conflict {
      Button("Keep Mine") { on press { await doc.saveAnyway() } }
      Button("Take Theirs") { on press { await doc.revert() } }
    }
  }
}
```

## ネットワーク：asuka/http

`asuka/http` はプラットフォーム自身のクライアントでリクエストを送るので、プロキシ、信頼された証明書、キャッシュ、Cookie はシステムと同じように働きます。リクエストは待つ処理なので `async` です。

```asuka
import asuka/http

struct Release {
  name: string
}

async fn latest() throws -> string {
  let r = try await http.get("https://api.github.com/repos/egoist/asuka/releases/latest")
  if !r.ok() { throw Error("HTTP {r.status}") }
  let rel: Release = try r.json()
  return rel.name
}
```

ブラウザーでは、別のオリジンへのリクエストにはそのオリジンの CORS ヘッダーが必要です。

大きなレスポンス（動画やアーカイブ）は、届くそばからファイルに書けます。`http.download(url, path, progress:)` は、パスの隣のファイルに書き、すべて届いたらパスへ名前を変えます。そのあいだ 1 秒に数回、届いたバイト数と全体のバイト数（サーバーが示さなければ -1）を渡して `progress` を呼びます。`timeout` は全体ではなく、次のデータを待つ最長の時間です。タスクをキャンセルするとダウンロードは止まり、ファイルは残りません。iOS と macOS では URLSession、Android では `HttpURLConnection`、Windows と Linux では WinHTTP と libsoup がそのままファイルへ書き、ブラウザーでは fetch のストリームから読んでアプリのファイルに書きます。

```asuka
import asuka/fs
import asuka/http

component Download(url: string) {
  state got = 0
  state total = -1
  state problem = ""

  task(url) {
    let file = fs.join([fs.appDir(), "downloads", "video.mp4"])
    do {
      try fs.mkdirAll(fs.dir(file))
      let r = try await http.download(url, file, progress: (n, all) => {
        got = n
        total = all
      })
      if !r.ok() { problem = "HTTP {r.status}" }
    } catch e {
      problem = e.message
    }
  }

  Column {
    Progress { value: if total > 0 { got.float() / total.float() } else { none } }
    if !problem.isEmpty() { Text(problem) }
  }
}
```

デスクトップアプリの OAuth サインインは、ループバックインターフェース上のアプリ自身のサーバーへリダイレクトします（RFC 8252）。`http.loopback(path:, page:)` はシステムが選ぶポートで待ち受け、その `url` が渡すべきリダイレクト URI です。`next()` はブラウザーが戻ってくるのを待ち、`page`（「このウィンドウは閉じてかまいません」）で応答して、ブラウザーが来た URL を返します。そのクエリがコードです。`close()` で待ち受けを止めます。Android と iOS も、アプリの実行中は待ち受けられます。ブラウザーはできないので（そこではエラーを投げます）、Web アプリのサインインは自分のサイトへリダイレクトします。

```asuka
import asuka/http

async fn signInBack(authorize: string) throws -> string {
  let lb = try await http.loopback()
  defer { lb.close() }
  openURL("{authorize}&redirect_uri={lb.url}")
  let back = try await lb.next() // http://127.0.0.1:port/callback?code=…
  return back
}
```

## ファイルの監視：asuka/watch

`watch.path(dir)` はフォルダー（またはファイル）の変更を追います。アプリが表示しているフォルダー、エディターで開いているプロジェクト、別のプログラムが書き込む設定ファイルなどです。その `events` は、変更のバッチが次々に届くストリームです。

```asuka
import asuka/watch

component Folder(dir: string) {
  state changes: [string] = []
  state problem = ""

  task(dir) {
    do {
      let w = try await watch.path(dir, ignore: [".*", "node_modules/"])
      for await events in w.events {
        for e in events { changes.push("{e.op} {e.path}") }
      }
    } catch e {
      problem = e.message
    }
  }

  Column {
    if !problem.isEmpty() { Text(problem) }
    for c in changes { Text(c) }
  }
}
```

監視は見たファイルの状態を覚えておき、システムが何か変わったと伝えるたびに見直して比較します。イベントはその差分であり、差分を生んだ一つひとつの操作ではありません。何度も続けて書き込まれたファイルは一回の `write`、作られてすぐ消されたファイルは何も起きなかったことになり、同じパスで置き換えられたファイル（エディターやアトミックな保存が行う方法）は `write`、フォルダーの中で移動したファイルやフォルダーは `oldPath` 付きの `rename` になります。イベントは「ファイルを読み直す合図」として扱ってください。システムが変更を取りこぼしたときは、監視がすべてを見直し、そのバッチは `overflow` から始まります。

`ignore` は `.gitignore` の簡単なパターン（`*.log`、`node_modules/`、`/dist`、`src/**/gen`）を受け取ります。`recursive: true` で中のフォルダーも監視し、`attributes: true` で権限の変更も報告します。各デスクトップ OS 自身の方法を使います。macOS では FSEvents（一つのストリームでツリー全体を監視）、Linux では inotify（フォルダーごとに一つの watch、上限は `fs.inotify.max_user_watches`）、Windows では `ReadDirectoryChangesW` です。スマートフォンと Web には監視の手段がなく、`watch.path` は unsupported エラーを投げます。ループを抜けると（またはコンポーネントがなくなると）監視は止まります。[`examples/files`](../../examples/files) は、リアルタイムに更新されるフォルダーの例です。

## メインスレッド

UI はメインスレッドで動いており、そこで待つ処理は UI を止めてしまいます。データについてまとめると次のとおりです。

- **メインスレッドの外で、await で待つもの**：`db.exec`、`db.query`、ライブクエリの各取得、`icloud` の呼び出し、`document` の読み書き、`http` のリクエスト。これらの `await` の間も UI は動き続けます。
- **メインスレッドで、すぐ終わるもの**：`sqlite.open` と `db.migrate`（アプリの起動時に）、`storage.Value` と `asuka/store`。後者はプラットフォームの設定を読み書きします（Windows と Linux ではメモリで、ファイルは少し後に書かれます）。

メインスレッドを止めてしまう自分の処理（大きなファイルの解析、画像の縮小など）は `spawn { … }` に入れると、別のスレッドで実行されます（[非同期処理](language.md#非同期処理)）。
