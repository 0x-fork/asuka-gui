# not-bindable

変化を伝えないプロパティの `bind`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`bind` は状態を渡し、要素が変化を伝えたときに書き戻します。バインドできるのは、コントロール自身の値（`TextField` の `text`、`Toggle` の `on`）か、コンポーネントが `event <prop>Change` を宣言している prop です。

## 直し方

コントロールの値をバインドするか、値を渡して（`text: s`）イベントを処理します。

## 誤った例

```asuka
component Name {
  state s = ""
  Text(s) { bind text: s }
}
```

## 正しい例

```asuka
component Name {
  state s = ""
  TextField { bind text: s }
}
```
