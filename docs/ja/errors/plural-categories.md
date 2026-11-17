# plural-categories

複数形や select のカテゴリーが言語のものと合わないメッセージ。

コンパイラーが報告する警告です。プログラムはビルドできますが、確認する価値があります。

## 理由

言語ごとに複数形のカテゴリーがあります（英語は `one` と `other`、アラビア語は 6 つ）。言語にないカテゴリーは選ばれることがなく、足りないカテゴリーは `other` にフォールバックします。

## 直し方

メッセージが挙げるとおり、その言語のカテゴリーを書きます。

## 誤った例

`app.asuka`

```asuka
component Count {
  Text(tr.items(count: 2))
}
```

`locales/en.json`

```json
{"items": "{count, plural, one {# item} few {# items} other {# items}}"}
```

## 正しい例

`app.asuka`

```asuka
component Count {
  Text(tr.items(count: 2))
}
```

`locales/en.json`

```json
{"items": "{count, plural, one {# item} other {# items}}"}
```
