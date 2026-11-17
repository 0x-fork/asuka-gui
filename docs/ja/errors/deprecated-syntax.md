# deprecated-syntax

置き換えられた古い書き方（`asuka fmt` が書き直す）。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

要素は、スタイルがほかのスタイルを適用するのと同じく、`style:` でスタイルを適用します。ブロックにスタイル名だけを書くのは古い形です。

## 直し方

`asuka fmt -w` を実行するか、`style: name` と書きます。

## 誤った例

```asuka
style card {
  padding: 16
}

component Note {
  Column {
    style card
    Text("hi")
  }
}
```

## 正しい例

```asuka
style card {
  padding: 16
}

component Note {
  Column {
    style: card
    Text("hi")
  }
}
```
