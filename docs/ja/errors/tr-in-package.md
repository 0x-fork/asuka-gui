# tr-in-package

アプリではないパッケージでの `tr`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

カタログはアプリのものです。ライブラリには、それを使うアプリにどんなメッセージがあるかわかりません。

## 直し方

テキストをパラメーターとして受け取り、アプリから `tr.key()` を渡してもらいます。

## 誤った例

`app.asuka`

```asuka
import @acme/ui

component Home {
  ui.Header()
}
```

`asuka.toml`

```toml
name = "home"

[dependencies]
"@acme/ui" = "file:./ui"
```

`locales/en.json`

```json
{"title": "Home"}
```

`ui/asuka.toml`

```toml
name = "@acme/ui"
version = "1.0.0"
```

`ui/ui.asuka`

```asuka
pub component Header {
  Text(tr.title())
}
```

## 正しい例

`app.asuka`

```asuka
import @acme/ui

component Home {
  ui.Header(tr.title())
}
```

`asuka.toml`

```toml
name = "home"

[dependencies]
"@acme/ui" = "file:./ui"
```

`locales/en.json`

```json
{"title": "Home"}
```

`ui/asuka.toml`

```toml
name = "@acme/ui"
version = "1.0.0"
```

`ui/ui.asuka`

```asuka
pub component Header(title: string) {
  Text(title)
}
```
