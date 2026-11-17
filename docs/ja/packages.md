# パッケージ

パッケージを使うと、Asuka のコードを複数のアプリで共有できます。共有できるのは、コンポーネント、ストア、ネイティブパッケージ（Swift、Kotlin、JavaScript のコードを含む）、C（ソース、またはベンダーのビルド済みライブラリ）です。パッケージは **npm レジストリ**で公開されますが、インストールするのは npm ではなく `asuka` CLI で、Node は必要ありません。

## パッケージの使い方

```sh
asuka add @acme/charts            # 最新のバージョン
asuka add @acme/charts@^1.2       # バージョンの範囲
asuka remove @acme/charts
```

`asuka add` は依存関係を `asuka.toml` に書き込み、バージョンを解決して `asuka.lock` に記録します。

```toml
[dependencies]
"@acme/charts" = "^1.2.0"
```

あとは名前でインポートします。

```asuka
import @acme/charts

charts.LineChart(points) { height: 200 }
```

`asuka dev`、`asuka build`、`asuka test` は、`asuka.lock` に書かれたパッケージを初回に取得し、以降はそれを使います。全員が同じバージョンでビルドできるように、`asuka.lock` はコミットしてください。`asuka install` はすべてを前もって取得します（CI 向け）。`asuka install -update` は `asuka.toml` の範囲からバージョンを解決し直します。

### パッケージの取得元

| asuka.toml の記述 | 意味 |
|---|---|
| `"@acme/charts" = "^1.2.0"` | レジストリにあるバージョンの範囲 |
| `charts2 = "npm:@acme/charts@^2.0.0"` | 別のパッケージとして `charts2` の名前でインポートするもの。二つ目のメジャーバージョンに使う |
| `charts = "github:acme/charts#v1.2.0"` | GitHub リポジトリのタグ、ブランチ、コミット |
| `charts = "file:../charts"` | プロジェクトの隣にあるディレクトリ。両方を並行して開発するとき |

プライベートなパッケージは、`.npmrc` に書かれたレジストリから、そこにあるトークンを使って取得します。`.npmrc` の読み方は npm と同じです。スコープ用の社内レジストリ（`@acme:registry=https://npm.acme.dev/`）、GitHub Packages、Verdaccio などが使えます。`NPM_TOKEN` は、デフォルトのレジストリのトークンです。

### バージョンの選び方

アプリの中で、各パッケージのバージョンは**一つだけ**です。そのパッケージを求めるすべての範囲を満たす、最も低いバージョンが選ばれます（Go modules と同じ、最小バージョン選択）。そのため、誰かが新しいバージョンを公開してもビルドは変わらず、同じ `asuka.lock` を持つ二人は同じものをビルドします。

メジャーバージョンが違えば、別のパッケージです。依存先の一つが `@acme/charts` の 1.x を必要とし、自分は 2.x を使いたい場合は、2.x を別の名前で依存関係に加え（`charts2 = "npm:@acme/charts@^2.0.0"`）、その名前でインポートします。この状況になると、`asuka add` がそのことを知らせます。

### 検証

tarball はどれも、使う前に `asuka.lock` に記録されたハッシュ（SHA-512、つまりレジストリの integrity）と照合されます。そのうえで、そのハッシュをキーにして、すべてのプロジェクトで共有するキャッシュ（Linux では `~/.cache/asuka/pkg`、ほかのシステムではシステムのキャッシュディレクトリ）に保存されます。インストール時にパッケージの中身が実行されることはありません。インストールスクリプトという仕組みがないからです。

## パッケージの作成

パッケージはライブラリです。`asuka.toml` と `.asuka` ファイルを入れたディレクトリで、`asuka new --lib ./charts` で作れます。中には関数とそのテストブロックが入っています（`asuka new` だけならアプリを作ります）。

```toml
name = "@acme/charts"
version = "1.2.0"
description = "Charts drawn with Asuka's Canvas"
license = "MIT"
repository = "github:acme/charts"

[dependencies]
"@acme/colors" = "^0.3.0"
```

パッケージの `pub` 宣言が、アプリからインポートできるものになります。パッケージにはネイティブコード（[ネイティブコード](native.md)）や C（自身のソースや、ベンダーのビルド済みライブラリ。[Asuka から C を呼び出す](c.md)）も含められ、それらも一緒に公開されます。

### ドキュメントを書く

パッケージのドキュメントはソースに書きます。最初の宣言の上にある `// Package <name> …` というコメントでパッケージが何かを説明し、`pub` 宣言のすぐ上の `///` コメントでその宣言が何をするかを書きます。構造体のフィールド、列挙型のケース、メソッドの上の `///` コメントは、それぞれが何かを説明します。周りより 2 文字深くインデントした行は例で、Asuka のコードとして表示されます。

```asuka
// Package @acme/charts は Asuka の Canvas でチャートを描きます。棒、
// 折れ線、円グラフがあり、データが変わるとアニメーションします。
//
//   Bars(data: sales) { color: .blue }

/// 値ごとに棒を一本描き、最大値に合わせて縮尺します。
pub component Bars(data: [float]) {
  …
}
```

`asuka doc` はパッケージのドキュメントを Markdown で出力します。まずパッケージのコメント、次に `pub` 宣言それぞれのシグネチャとコメントが、関数、コンポーネント、store、型に分けて並びます。インポートパス、ディレクトリ、または何も渡さなければ（このディレクトリのパッケージ）そのパッケージを、名前を加えればその宣言だけを出力します。

```sh
asuka doc                         # このディレクトリのパッケージ
asuka doc @acme/colors            # 依存パッケージ
asuka doc std/time Time.add       # 一つの宣言
asuka doc ./charts -o API.md      # ファイルに書き出す
```

このサイトの標準ライブラリとプラットフォーム SDK のページも、同じようにソースから作られています。エディタでは、ホバーすると `///` コメントが表示されます（[エディタ](development.md#エディター)）。

### 公開

```sh
asuka pkg publish                 # npm のログイン情報か NPM_TOKEN を使う
asuka pkg publish -otp 123456     # 2 要素認証を設定したアカウント
asuka pkg publish -tag next       # プレリリース
```

`asuka pkg publish` は、ディレクトリ（`.git`、`node_modules`、`dist`、`asuka.lock` を除く）を毎回同じ方法でまとめます。そのため、同じソースからは同じ tarball ができます。続いて、レジストリが読む `package.json` を書き出し、アップロードします。`npm login` で一度ログインするか、`NPM_TOKEN` を設定してください。CI で npm を使って公開すると（`asuka pkg pack` が書き出したディレクトリで `npm publish --provenance` を実行）、来歴の証明（provenance statement）が付きます。

バージョンは semver に従います。アップロードの前に、`asuka pkg publish` はパッケージの `pub` API を直前のバージョンと比べます。宣言を削除したり、利用者のコードが動かなくなるような形で変更したりするには、新しいメジャーバージョンが必要です。

## 標準ライブラリとプラットフォーム SDK

`std/http`、`std/json`、`std/storage` などの標準ライブラリと、`platform/camera`、`platform/notify` などのプラットフォーム SDK のパッケージは CLI に同梱され、CLI と同じバージョンで管理されます。`asuka add` は必要ありません。パッケージに `std/` や `platform/` で始まる名前は付けられません（`asuka pkg publish` も `asuka add` も受け付けません）。それぞれの API はリファレンスにあります：[標準ライブラリ](std.md)と[プラットフォーム SDK](platform.md)（ターミナルでは `asuka doc std/time`）。
