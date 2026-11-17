# embed-file

パッケージにないファイルの `embed`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`embed.text(path)` などは、コンパイル時にファイルをアプリに取り込みます。パスは文字列リテラルで、パッケージのディレクトリからの相対パスで、その中になければなりません。

## 直し方

パスを直すか、ファイルをパッケージに追加します。

## 誤った例

```asuka
import std/embed

let terms = embed.text("terms.md")
```

## 正しい例

`app.asuka`

```asuka
import std/embed

let terms = embed.text("terms.md")
```

`terms.md`

```markdown
# Terms
```
