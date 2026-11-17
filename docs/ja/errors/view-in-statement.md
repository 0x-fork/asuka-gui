# view-in-statement

実行されるコードの中で作られたビュー。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

ビューはコンポーネントの本体で宣言し、それを決める条件が成り立つあいだ存在し続けます。ハンドラーや関数の中で作ったビューは、作られてすぐ捨てられ、どこにも表示されません。

## 直し方

ビューを本体で宣言し、ハンドラーが設定する状態の `if` の下に置きます。

## 誤った例

```asuka
component Hello {
  Button("Greet") {
    on press { Text("Hi!") }
  }
}
```

## 正しい例

```asuka
component Hello {
  state greeted = false
  Column {
    Button("Greet") { on press { greeted = true } }
    if greeted { Text("Hi!") }
  }
}
```
