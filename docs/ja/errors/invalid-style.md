# invalid-style

受け付けない形で宣言・適用されたスタイル。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

スタイルに書けるのはスタイルプロパティ、`style:`、`use`、バリアントです。`style:` は 1 回だけ、スタイルかそのリスト（`style: [card, pink]`）に設定します。リストにはスタイルか、それを選ぶ `if` や `match` を書きます。

## 直し方

スタイルを 1 つのリストにまとめます（`style: [a, b]`）。

## 誤った例

```asuka
style big {
  font: .title
}

style red {
  color: .red
}

component Title {
  Text("Hi") {
    style: big
    style: red
  }
}
```

## 正しい例

```asuka
style big {
  font: .title
}

style red {
  color: .red
}

component Title {
  Text("Hi") {
    style: [big, red]
  }
}
```
