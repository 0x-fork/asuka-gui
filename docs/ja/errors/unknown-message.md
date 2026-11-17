# unknown-message

カタログにないメッセージの `tr.key`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`tr` はアプリのメッセージを表示します。各メッセージはソース言語のカタログ（`locales/en.json`）のキーに対応します。存在しないキーには、どの言語のテキストもありません。

## 直し方

カタログにあるキーを使う（メッセージが提案します）か、カタログに追加します。

## 誤った例

`app.asuka`

```asuka
component Title {
  Text(tr.helo())
}
```

`locales/en.json`

```json
{"hello": "Hello"}
```

## 正しい例

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/en.json`

```json
{"hello": "Hello"}
```
