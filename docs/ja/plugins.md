# プラグイン

プラグインは、ネイティブのライブラリを同梱したパッケージです。ライブラリは C、C++、Zig、Rust で書かれ、プラットフォームごとにビルド済みの状態で一度だけ公開されます。そのパッケージを使うアプリは、どれもこのライブラリを一緒に配布します。パッケージの `asuka.toml` の隣にあるマニフェスト `asuka-plugin.toml` が、ライブラリごとに各プラットフォームのファイルとその SHA-256 を記しています。`std/sqlite` もプラグインの一つで（[データ](data.md#レコードstdsqlite)）、SQLite をすべてのプラットフォーム向けに同じオプションで一度だけビルドしたものです。

## プラグイン、ネイティブパッケージ、C

Asuka 以外で書かれたコードを取り込む方法は三つあります。一つのパッケージで複数を組み合わせることもできます。

| | 何か | ビルドされるとき | 向いている用途 |
|---|---|---|---|
| [`extern` で呼ぶ C](c.md) | `asuka.toml` の `[c]` に挙げた C のソース。Asuka から `extern fn` で呼ぶ | アプリと一緒に、CLI のコンパイラーがターゲットごとに | 自分で書いた C、小さなライブラリのソース、アプリに直接組み込みたい C |
| [ネイティブパッケージ](native.md) | Swift、Kotlin、JavaScript、C で実装する `native interface` や `native component` | アプリと一緒に、各プラットフォームのプロジェクト（Swift パッケージ、Gradle モジュール、`web/`、`desktop/`）から | プラットフォーム自身の API とビュー |
| プラグイン | プラットフォームごとにビルド済みのライブラリ。`asuka-plugin.toml` に記す | プラグイン独自のビルドで一度だけ。その後公開する | 大きなライブラリ、独自のツールチェーンが要るもの（Rust、configure の手順）、全プラットフォームで同じビルドでなければならないもの |

プラグインのライブラリはアプリに組み込まれません。それぞれが独立したライブラリ（`.dylib`、`.so`、`.dll`、iOS のフレームワーク、WebAssembly モジュール）で、実行時にパッケージのコードが読み込みます。アプリのビルドでこれらがビルドされることはないので、SQLite を使うアプリが SQLite をコンパイルすることもありません。

## プラグインの使い方

パッケージをインポートします。

```asuka
import std/sqlite

fn openNotes() throws -> sqlite.DB = try sqlite.open("notes.db")
```

必要なのはこれだけです。`asuka build` は、アプリが使うパッケージ（アプリ自身のディレクトリ、依存パッケージ、インポートした標準ライブラリとプラットフォーム SDK のパッケージ）の中からプラグインを見つけ、ビルド先のプラットフォーム向けに各ライブラリのファイルを同梱します。`asuka dev`、`asuka test`、`asuka run` は、プラグインがビルド済みのチェックアウトならそのビルドを、そうでなければ公開済みのファイルを使います。公開済みのファイルは初回にユーザーのキャッシュ（システムのキャッシュディレクトリの `asuka/plugins`）へダウンロードされ、以後はそこから使われます。ダウンロードしたファイルは、使う前に必ずマニフェストに記された SHA-256 と照合されます。レジストリから取得したパッケージはライブラリを含んでおらず、その置き場所をマニフェストが示しています。

### ライブラリの置き場所

| プラットフォーム | 場所 | |
|---|---|---|
| macOS | `Contents/Frameworks` | アプリと一緒に署名 |
| Windows | `.exe` の隣 | `ASUKA_WINDOWS_CERT` で `.exe` と一緒に署名 |
| Linux | インストール先の `lib/<app>` | tarball と Flatpak に含まれる |
| iOS | `Frameworks/<library>.framework` | 専用の Info.plist とプライバシーマニフェストを持ち、アプリと一緒に署名 |
| Android | 各 ABI の `libasuka.so` の隣 | |
| Web | ホストのファイルの `lib/` | ページの `libs` オプションで各ファイルの URL を指定 |

アプリのプラグインのライブラリはすべて一か所に置かれるため、同じプラットフォームで同名のファイルを持つプラグインを二つ使うことはできません。その場合は、ビルドがどの二つかを教えてくれます。

開発中は、`asuka dev` が開発用アプリから見つけられる場所にライブラリを置きます。デスクトップではコアに各ファイルを直接指定し（`ASUKA_LIB_<NAME>`。ライブラリ名を大文字にしたもので、`ASUKA_LIB_ASQLITE` など）、Android では開発用アプリのコアの隣へ転送し、iOS では開発用アプリ内のフレームワークになり、Web ではページの `asuka/lib/` から配信します。`asuka test` と `asuka run` も `ASUKA_LIB_<NAME>` を設定します。

別のアプリに組み込むアプリ（`asuka build --platform ios --xcframework`）には、ライブラリごとの XCFramework が付いてくるので、組み込む側のアプリもそれを埋め込みます。

## プラグインの作成

プラグインは、`asuka.toml` の隣に `asuka-plugin.toml` を置いたパッケージ（[パッケージ](packages.md)）に、ライブラリを作るビルドと、それを読み込むコードを加えたものです。

### マニフェスト

```toml
name = "sqlite"
sources = ["csrc"]

[[libraries]]
name = "asqlite"
version = "3.54.1"
release = "https://github.com/you/sqlite-plugin/releases/download/plugin-sqlite"

[libraries.files]
darwin = {name = "libasqlite.dylib"}
ios = {name = "asqlite"}
ios-simulator = {name = "asqlite"}
android-arm64 = {name = "libasqlite.so"}
android-arm = {name = "libasqlite.so"}
android-amd64 = {name = "libasqlite.so"}
linux-amd64 = {name = "libasqlite.so"}
linux-arm64 = {name = "libasqlite.so"}
windows-amd64 = {name = "asqlite.dll"}
windows-arm64 = {name = "asqlite.dll"}
wasm = {name = "asqlite.wasm"}
```

- `name` はプラグインの名前で、CLI のメッセージやリリースで使われます。英数字、`-`、`_`、`.` が使え、先頭に記号は置けません。
- `sources` はライブラリのビルド元で、マニフェストからの相対パスです（ディレクトリはその中のファイルすべて）。古いソースからビルドしたライブラリは公開されません。省略すると、`build/`、マニフェスト、`testdata`、ドットで始まるファイルを除く、パッケージのすべてのファイルが対象です。
- 各ライブラリの `name` は、コードがそのライブラリを探すときの名前（`ASUKA_LIB_ASQLITE`）で、iOS ではフレームワークの名前にもなります。`version` はライブラリのバージョンで、そのフレームワークの Info.plist に入ります。`release` はファイルの公開先の GitHub リリースで、`https://github.com/<owner>/<repo>/releases/download/<tag>` の形です。
- `files` にはプラットフォームごとのファイルを書きます。`darwin`（arm64 と x86_64 の両方の Mac 向けのユニバーサルなファイル一つ）、`ios` と `ios-simulator`、`android-arm64`、`android-arm`、`android-amd64`、`linux-amd64`、`linux-arm64`、`windows-amd64`、`windows-arm64`、`wasm` です。ファイルのないプラットフォーム向けにビルドすると、その旨を示して失敗します。Mac のファイルは `.dylib` で、iOS のファイルはライブラリ名にします（フレームワークのバイナリで、install name は `@rpath/<library>.framework/<library>`）。

公開した内容（`asset`、`size`、`sha256`、`rawSize`、`rawSHA256`、`source`）は `asuka plugin publish` が各ファイルの項目に書き足します。自分では書かないでください。

### ビルド

プラグインは、ライブラリに合わせた独自のツールでビルドします。sqlite プラグインは、SQLite とそのエントリーポイントを、Mac と iOS 向けには Xcode、Android 向けには NDK、Windows、Linux、Web 向けには Zig でコンパイルします。シェルスクリプトでも、Makefile でも、プログラムでも構いません。ターゲットごとに、ライブラリをマニフェストの隣の `build/<target>/<file>` に書き出し、その隣に、ビルド元のソースのハッシュを入れた `<file>.source` を置きます。ハッシュは、`sources` のファイルをパス順（マニフェストからの相対パス、区切りは `/`）に並べ、それぞれについて「パス、空白、バイト単位のサイズ、改行」とファイルの中身を続けたものの SHA-256 です。

`build/` はリポジトリにコミットせず、パッケージの tarball にも含まれません。チェックアウトの中では、`asuka dev`、`asuka test`、`asuka run` は公開済みのファイルより先にそこのビルドを使うので、プラグイン自身のテストや隣にあるアプリは、ビルドしたばかりのライブラリで動きます。

### ライブラリの読み込み

パッケージのコードは、プラットフォームのローダー（`dlopen`、`LoadLibrary`、WebAssembly のインスタンス化）でライブラリを読み込み、関数を探します。そのため、アプリはリンク時にライブラリに依存しません。探す順番は次のとおりです。

1. `ASUKA_LIB_<NAME>` に入っているファイルのパス。`asuka dev`、`asuka test`、`asuka run` が設定します。
2. `asuka build` が置いた場所。実行ファイルのディレクトリから見て、Mac では `../Frameworks/<file>`（`Contents/MacOS` から）、Windows では `.exe` と同じディレクトリ、Linux では `../lib/<exe>/<file>`（`bin` から）、iOS では `Frameworks/<library>.framework/<library>`（アプリ拡張機能からは `../../Frameworks/…`）です。
3. 名前だけで、システムのローダーが探す場所。Android では `lib<library>.so`（APK の `lib/<abi>`）、iOS では `@rpath/<library>.framework/<library>` です。

Web では、ページの `libs` オプションが各ファイル名をその URL に対応づけます。

`std/sqlite` は、最初にデータベースを開くときに `asqlite` を読み込み、メインスレッドの外で動かします。Web では SQLite のワーカーが `asqlite.wasm` をインスタンス化します。自分のパッケージでは、その[ネイティブコード](native.md)、つまりデスクトップの C、Swift、Kotlin、Web の JavaScript からライブラリを読み込みます。

ライブラリが公開するシンボルは少なく、自分のものだけにして、リンクしたものは公開しないでください。ビルド元と同じコードの別のコピーが、同じプロセスにあるかもしれません（GTK が読み込むシステムの SQLite など）。呼び出しは値ごとに関数を呼ぶのではなく、操作をまるごと一回で行います。sqlite プラグインのエントリーポイントは `asq_call` 一つだけで、文を一つまるごと実行し、その行を一つのバッファにまとめて返します。だからこそ、Web ではワーカーで動かせます。

### 公開

```sh
asuka plugin publish path/to/plugin     # asuka-plugin.toml のあるディレクトリ（省略時: .）
```

`asuka plugin publish` は、各ライブラリのビルドを gzip で圧縮し、ハッシュにちなんだ名前を付けます（`asqlite-darwin-1982f596dd7c-libasqlite.dylib.gz`）。リリースにまだないファイルを `gh` でアップロードし、リリースがなければ作成します。アップロードしたファイルはそれぞれダウンロードし直して確認し、マニフェストに記録します。コミットするのはこのマニフェストです。マニフェストはその旨を記したコメントの下にまるごと書き直されるので、自分で書いたコメントは残りません。リリースにあるファイルが置き換えられることはないため、過去のどのコミットのマニフェストも使え続けます。ビルドがプラグインのソースより古ければ公開は止まるので、先にビルドし直してください。

あとは、ほかのパッケージと同じようにパッケージを公開します（`asuka pkg publish`、[パッケージ](packages.md)）。tarball にはマニフェストが含まれ、依存するアプリは、ビルドするプラットフォームの分だけライブラリをリリースからダウンロードします。
