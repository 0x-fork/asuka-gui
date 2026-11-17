# invalid-variant

成り立たない形で宣言・使用されたバリアント。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

バリアントは、条件が成り立つあいだ、コンポーネントが表示する 1 つの要素に重ねられます。中身はそれに合わせて変わるスタイルプロパティで（周りのビューを動かすレイアウトは含まず）、中にバリアントは入れられません。

## 直し方

要素を 1 つだけ表示し（ビューを `Row` や `Column` に入れる）、バリアントにはそれに合わせて変わるものだけを書きます。

## 誤った例

```asuka
component Chip {
  prop selected = false
  variant selected = selected
  Text("a")
  Text("b")
}
```

## 正しい例

```asuka
component Chip {
  prop selected = false
  variant selected = selected
  Row {
    Text("a")
    Text("b")
  }
}
```
