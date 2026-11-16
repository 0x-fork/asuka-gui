# iCloud

`asuka/icloud` を使うと、iOS と macOS のアプリからユーザーの iCloud を使えます。このパッケージは二つの部分からなります。**キーバリューストア**は小さな値（設定、最後に読んだページ、連続記録など）を保存し、その値はユーザーのどのデバイスにも届きます。**書類**は iCloud Drive にあるアプリのファイルで、「ファイル」アプリや Finder では、アプリの名前のフォルダーに表示されます。Swift でいえば NSUbiquitousKeyValueStore とアプリの ubiquity コンテナーに、Expo アプリでいえば react-native-cloud-storage や expo-icloud-storage が提供するものにあたります。

```asuka
import asuka/icloud

let size = icloud.Value("textSize", 17.0)   // どのデバイスでも同じ値

component Entries {
  let files = icloud.files(.documents)      // 常に最新の一覧

  Column {
    Text("Text size: {size.value}")
    for f in files key f.path {
      Text(f.name)
    }
    Button("Write today's") {
      on press {
        let dir = try await icloud.documents()   // iCloud Drive にあるアプリのフォルダー
        try await icloud.writeFile("{dir}/2026-11-15.txt", "Rain.".bytes())
      }
    }
  }
}
```

iCloud は Apple のサービスです。Android、Windows、Linux、Web ではアカウントがなく、一覧は空で、`Value` はデフォルト値と、デバイス上で設定された値を保つだけです。呼び出しは「icloud: not available」を投げます。これらのプラットフォームでは、自前のサーバーを通じて同期してください。

[examples/journal](../../examples/journal) は、一日一つのテキストファイルとして日記を iCloud Drive に保存し、文字の大きさをキーバリューストアに保存します。

## アカウント

`icloud.account()` は追跡され、ユーザーが iCloud にサインインしていて、アプリの iCloud がオンになっているか（`available`）と、それがどのアカウントか（`id`）を表します。バインディングの中で読むと、読まれているあいだはアカウントの変化に追従します。変わるのは、ユーザーがサインインやサインアウトをしたとき、アプリの iCloud をオフにしたとき、別のユーザーがサインインしたときです。`icloud.currentAccount()` は、追跡せずに一度だけ読みます。

```asuka
import asuka/icloud

component SignInHint {
  let account = icloud.account()

  if !account.available {
    Text("Sign in to iCloud to keep your journal on all your devices.")
  }
}
```

`id` は iCloud の ID トークン（identity token）から得られる、中身に意味のない値です。一つのデバイスの上では、同じアカウントはいつも同じ ID になります（デバイス間で比べるためのものではありません）。この ID を保存しておき、アプリの起動時に別の ID になっていたら、Apple が勧めているとおり、前のユーザーの iCloud から得たデータは使わずに取り分けておいてください。

アカウントがない場合、書類の呼び出しは例外を投げます。キーバリューストアはデバイス上でそのまま動き続け、ユーザーがサインインすると同期されます。

## キーバリューストア

`icloud.Value(key, default)` は、`value` がキーの値になるストアです。最初は `default` で、読まれると iCloud が保持している値になり、使われているあいだは、ほかのデバイスでの変更に追従します。`value` に代入すると、その値は iCloud に保存されます。デバイスにはすぐに、ユーザーのほかのデバイスには iCloud が同期したときに保存されます。値には JSON（`asuka/json`）で表せるものなら何でも使えます。数値、文字列、配列、構造体などです。

```asuka
import asuka/icloud

let lastPage = icloud.Value("lastPage", 0)

component Reader {
  Column {
    Text("Page {lastPage.value}")
    Button("Next page") { on press { lastPage.value += 1 } }
  }
}
```

`icloud.remove(key)` は、キーをストアから削除します。

二つのデバイスが同じキーに値を設定した場合は、最後に設定したほうが残ります。オフラインだったデバイスは、オンラインに戻ったときにほかのデバイスの変更を受け取り、そのときにそのデバイスの `Value` も変更に追従します。iCloud は自動で同期します。パッケージには、同期を早めるための呼び出しも、iCloud が行った変更とその理由（ほかのデバイスによる変更、初回のダウンロード、アカウントの変更、容量の超過）の一覧もありません。

ストアは小さな値のためのものです。キーは 64 バイトまで、値は 1 MB までです。一つのアプリの値は合わせて 1 MB、1024 キーまでで、これを超えると iCloud は値を同期しなくなります。それより大きなデータには書類を使ってください。

値はプロパティリストとして保存されるので、アプリの Swift、ホーム画面ウィジェット、Apple Watch のアプリからも同じように読めます。JSON の文字列、数値、真偽値、配列、オブジェクトは、プロパティリストの対応する型になります。オブジェクトの中の null は省かれますが、デコードした結果は変わりません。配列の中の null は保存できません。

## 書類

iCloud にあるアプリのファイルは、アプリの**コンテナー**に置かれます。コンテナーは、iCloud が同期する、デバイス上のディレクトリです。`icloud.container(id)` はコンテナーのディレクトリで、`""`（デフォルト）ならアプリの最初のコンテナーのディレクトリです。デフォルトでは、アプリのコンテナーは `iCloud.<the app's ID>` の一つだけです。`asuka.toml` の `icloud.containers` でほかのコンテナーを指定でき、その最初のものがデフォルトになります。初回の呼び出しは、iCloud がコンテナーを準備するため、時間がかかることがあります。

`icloud.documents()` は、最初のコンテナーの `Documents` ディレクトリです。ここに置いたものは、iCloud Drive（iOS では「ファイル」アプリ、Mac では Finder）のアプリの名前のフォルダーに表示されます。ユーザーはそこでファイルを開いたり、名前を変えたり、移動したりできます。コンテナーのそれ以外の部分は、アプリだけが使います。

### ファイルの一覧

`icloud.files(scope)` は追跡され、アプリのファイルを表します。`.documents` はコンテナーの `Documents` にあるファイル、`.data` はそれ以外のファイルです。一覧には、ほかのデバイスが作ったまま、まだダウンロードされていないファイルも含まれます。バインディングの中で読むと、ファイルの変化に合わせて一覧も変わります。変わるのは、ほかのデバイスからファイルが届いたとき、ファイルが追加、削除、名前の変更、ダウンロード、アップロードされたとき、ダウンロードが進んだときです。中身は iCloud のメタデータクエリ（NSMetadataQuery）で、最初の読み手が現れたときに開始し、最後の読み手がいなくなると停止します。`icloud.list(scope)` は、追跡せずに一度だけ一覧にします。

`File` には `path`、`name`、`dir`、`size`、`created`、`modified`（1970 年からのミリ秒）と、ファイルがどこにあるかを表すフィールドがあります。

| フィールド | 意味 |
| --- | --- |
| `status` | `.notDownloaded`（iCloud にだけある）、`.downloaded`（iCloud のものより古いバージョン）、`.current` |
| `downloading`、`downloadProgress` | ダウンロード中かどうかと、ダウンロード済みの割合（0〜1） |
| `uploading`、`uploadProgress`、`uploaded` | iCloud にアップロード中かどうか、アップロード済みの割合、iCloud にあるかどうか |
| `conflicts` | ほかのデバイスが同時にこのファイルのバージョンを保存した |
| `downloadError`、`uploadError` | 最後のダウンロードやアップロードが失敗した理由（デバイスの空き容量不足、iCloud の容量不足など） |

### 読み書き

アプリのファイルは `icloud.readFile(path)`、`writeFile(path, data)`、`delete(path)`、`rename(from:, to:)` で読み書きします。これらは iCloud 自身の読み書きと協調する（NSFileCoordinator）ので、同期処理が書きかけのファイルを見ることも、iCloud が置き換えている途中のファイルをアプリが読むこともありません。`readFile` は、デバイスにないファイルなら先にダウンロードし、それを待ちます。`writeFile` はファイル全体を一度に書き込み、必要なディレクトリも作ります。書き込んだファイルは iCloud がアップロードします。ダウンロード済みのファイルには `asuka/fs` の関数も使えますが、同期とのあいだで競合状態（レース）になることがあります。

`icloud.download(path)` は、ファイルのダウンロードを始めます。進み具合は、そのファイルの `File` でわかります。`icloud.evict(path)` は、ファイルを iCloud に残したままデバイスから取り除き、空き容量を増やします。取り除いたファイルは、次に読むときに再びダウンロードされます。

デバイス上のファイルを iCloud に移したり、iCloud のファイルをデバイス上に移したりする機能は、まだパッケージにありません。`writeFile` で書き込む（そしてデバイス上のコピーを削除する）か、`readFile` で読んで `asuka/fs` で書き込んでください。

### 競合

二つのデバイスが同時に（あるいは一方がオフラインのあいだに）ファイルを変更すると、iCloud は一方をそのファイルとして残し、残りをそのファイルと競合するバージョンとして保存します。このとき、ファイルの `conflicts` は true になります。`icloud.conflicts(path)` は、競合しているバージョンを返します。各 `Version` には、内容を読むための `path`、`modified`、保存したデバイスの名前（`device`）があります。`icloud.resolve(path, keeping:)` で競合を解決します。`keeping` に渡したバージョンがそのファイルになり、渡さなければファイルはそのまま残ります。どちらの場合も、ほかのバージョンはすべてのデバイスから削除されます。内容をマージするには、マージした結果を `writeFile` で書き込んでから `resolve(path)` を呼びます。

```asuka
import asuka/icloud

async fn keepTheOther(path: string) throws {
  let vs = try await icloud.conflicts(path)
  if let v = vs.first() {
    try await icloud.resolve(path, keeping: v)   // ほかのデバイスのものを残す
  }
}
```

### リンクの共有

ファイルの公開リンク（リンクを知っていれば誰でもファイルのコピーをダウンロードできるもの）は、まだ `asuka/icloud` にありません。

## asuka.toml とエンタイトルメント

アプリに付与されるものは、アプリが何を呼んでいるかで決まります。キーバリューストア（`Value`、`remove`）を使うアプリには、そのエンタイトルメント（`com.apple.developer.ubiquity-kvstore-identifier`。値はチームの ID とアプリの ID）が付きます。書類（`container`、`documents`、`files`、`list`、`readFile`、`writeFile`、`delete`、`rename`、`download`、`evict`、`conflicts`、`resolve`）を使うアプリには、iCloud Documents（`com.apple.developer.icloud-services` の `CloudDocuments`）とアプリのコンテナーが付き、Info.plist の `NSUbiquitousContainers` によって、最初のコンテナーの `Documents` が iCloud Drive に表示されます。アカウントについて問い合わせるだけのアプリにも、ストアのエンタイトルメントが付きます。これがないとアカウントを確認できないためです。

```toml
[icloud]
containers = ["iCloud.com.example.journal"]
driveName = "Journal"
```

| キー | 型 | デフォルト | 説明 |
| --- | --- | --- | --- |
| `containers` | 文字列の配列 | `["iCloud.<id>"]` | アプリの iCloud コンテナーです。最初のものがデフォルト（`container()`、`documents()`）になります。 |
| `driveName` | 文字列 | アプリの `name` | iCloud Drive での、最初のコンテナーのフォルダーの名前です。 |
| `driveHidden` | 真偽値 | `false` | アプリの書類を iCloud Drive に表示しません。書類は同期されますが、見えるのはアプリだけです。 |

実機向けのビルドやリリースビルドでは、developer.apple.com（Identifiers）で、アプリの App ID の iCloud ケイパビリティを有効にしておく必要があります。書類を使う場合は、iCloud Documents とアプリのコンテナーも有効にします。iPhone で `asuka dev` が使う開発用のプロファイルは Xcode が作り、そのときに Xcode がこの設定も済ませます。iCloud を含むプロファイルにはワイルドカード（サービスの `*`、ストアの `TEAMID.*`）が入っていますが、ビルドは Xcode と同じく、それをアプリが使う値にして署名します。プロファイルに足りないもの（ケイパビリティやコンテナー）は、`asuka build` が教えてくれます。

iCloud Drive が新しいフォルダーや新しい名前を反映するのは、`build` 番号が新しくなったときです。`icloud` を変更したら `build` を増やしてください。

## asuka dev の場合

iCloud は、アプリの値と書類をアプリ自身の ID で管理します。`asuka dev` の開発用アプリは、アプリの ID と、アプリが呼ぶものに応じたエンタイトルメントを持つ、アプリそのものとしてビルドされます。そのため、シミュレーターでも iPhone でも、ビルドしたアプリと同じように iCloud が動きます。シミュレーターで iCloud を試すには、シミュレーターの「設定」で Apple アカウントにサインインし、iCloud Drive をオンにしてください。

Mac では、iCloud のエンタイトルメントを使うには、アプリにプロビジョニングプロファイルで署名する必要があります（`asuka build --profile`、または `asuka.toml` の `mac.profile`）。`asuka dev` はこの署名をしないので、プロファイルがないと、呼び出しは例外を投げ、その理由を伝えます。

## プラットフォームごとの動作

| プラットフォーム | iCloud |
| --- | --- |
| iOS | NSUbiquitousKeyValueStore、アプリの ubiquity コンテナー、NSMetadataQuery、NSFileCoordinator、NSFileVersion |
| macOS | iOS と同じです。このパッケージをインポートするアプリのためにビルドが作る、アプリの Swift ライブラリの中で動きます。iCloud を含むプロビジョニングプロファイルが必要です |
| Android、Windows、Linux、Web | ありません（「icloud: not available」）。iCloud for Windows はユーザーの iCloud Drive を同期しますが、アプリにはストアもコンテナーも提供しません。Web の CloudKit JS にも、どちらもありません |

このパッケージは CloudKit のデータベース（レコード、クエリ、共有）は使いません。

## テスト

`asuka test` はフェイクホストで動き、フェイクホストには iCloud がありません。Android と同じく、アカウントは利用できず、一覧は空で、`Value` はデフォルト値と、テストが設定した値を保つだけです。呼び出しは「icloud: not available」を投げます。ユーザーのほかのデバイスの役をテストが演じられるフェイクの iCloud は、まだパッケージにありません。

```asuka
import asuka/icloud

test "no iCloud on the fake host" {
  expect(!icloud.account().available)
  expect(icloud.files(.documents).isEmpty())
  do {
    let _ = try await icloud.documents()
  } catch e {
    expect(e.message == "icloud: not available")
  }
}
```
