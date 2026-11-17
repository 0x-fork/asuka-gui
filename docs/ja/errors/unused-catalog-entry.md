# unused-catalog-entry

何にも使われていないカタログの説明や引数。

コンパイラーが報告する警告です。プログラムはビルドできますが、確認する価値があります。

## 理由

存在しないメッセージの説明（`@key`）、メッセージが使わないプレースホルダー、ソース言語の引数を落とした翻訳は、消し忘れか誤りです。

## 直し方

削除するか、メッセージの中で引数を使います。

## 誤った例

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/en.json`

```json
{"hello": "Hello", "@bye": {"description": "Leaving"}}
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
{"hello": "Hello", "@hello": {"description": "A greeting"}}
```
