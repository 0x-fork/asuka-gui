# invalid-ref

1 つの要素のビューではないものを指す `ref` や `as`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`ref: x` と `as x` は要素のプラットフォームのビューに名前を付け、それを読むもの（状態、スクロール）が使えるようにします。複数のビューを表示するコンポーネントには、名前を付けられる 1 つのビューがありません。

## 直し方

コンポーネントの中の要素に名前を付けるか、コンポーネントが表示する要素を 1 つにします。

## 誤った例

```asuka
component Pair {
  Text("a")
  Text("b")
}

component Page {
  Pair() as p
}
```

## 正しい例

```asuka
component Pair {
  Row {
    Text("a")
    Text("b")
  }
}

component Page {
  Pair() as p
}
```
