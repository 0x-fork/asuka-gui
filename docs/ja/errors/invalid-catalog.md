# invalid-catalog

メッセージのファイルとして正しくないカタログ。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

カタログ（`locales/<言語>.json`）はメッセージからなる JSON オブジェクトです。キーは名前で、値は ICU 形式のメッセージ、その引数はソース言語のものと同じです。

## 直し方

メッセージが指す場所でファイルを直します。キーごとに文字列を書き、プレースホルダーはソース言語に合わせます。

## 誤った例

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/en.json`

```json
{"hello": 1}
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
