# missing-translation

ソース言語のメッセージの一部がない言語のカタログ。

コンパイラーが報告する警告です。プログラムはビルドできますが、確認する価値があります。

## 理由

ない言語のメッセージは、フォールバック先の言語で表示されます。動きはしますが、知っておく価値があります。

## 直し方

一覧にあるメッセージを、その言語のカタログで翻訳します。

## 誤った例

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/de.json`

```json
{"hello": "Hallo"}
```

`locales/en.json`

```json
{"hello": "Hello", "bye": "Bye"}
```

## 正しい例

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/de.json`

```json
{"hello": "Hallo", "bye": "Tschüss"}
```

`locales/en.json`

```json
{"hello": "Hello", "bye": "Bye"}
```
