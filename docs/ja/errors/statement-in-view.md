# statement-in-view

コンポーネントのビューのあいだにある文。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

コンポーネントの本体は宣言です。状態、let、そして成り立ち続けるビューを書きます。そこに文を書いても、1 回しか実行されないか、いつ実行されるのかがはっきりしません。ビューの中の match にはガードも付けられません。

## 直し方

文はハンドラー（`on press { … }`）、fn、タスク、エフェクトに書きます。導かれる値は `let` にします。

## 誤った例

```asuka
component Counter {
  state n = 0
  Column {
    n += 1
    Text("{n}")
  }
}
```

## 正しい例

```asuka
component Counter {
  state n = 0
  Column {
    Button("Add") { on press { n += 1 } }
    Text("{n}")
  }
}
```
