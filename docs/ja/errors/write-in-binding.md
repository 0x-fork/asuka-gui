# write-in-binding

UI が追従する場所で状態に書き込んでいる。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

バインディング、`let`、コンポーネントの本体は、成り立ち続けることを宣言します。読んでいるものが変わるたびに再び実行されるので、そこで書き込むと、自分が起こした変化のたびにまた実行されてしまいます。

## 直し方

状態にはハンドラー（`on press { … }`）、fn、タスク、エフェクトの中で書き込みます。状態から計算する値は `let` にします。

## 誤った例

```asuka
component Counter {
  state n = 0
  let shown = if n > 9 {
    n = 0
    0
  } else {
    n
  }
  Button("{shown}") { on press { n += 1 } }
}
```

## 正しい例

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { n = if n >= 9 { 0 } else { n + 1 } } }
}
```
