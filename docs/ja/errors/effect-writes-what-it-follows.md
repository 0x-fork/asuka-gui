# effect-writes-what-it-follows

自分が読む状態に書き込むエフェクト。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

エフェクトは、読んだものが変わると再び実行されます。それ自体に書き込むと、自分の書き込みのあとにもう一度、1 パス遅れて実行されます。ハンドラーはすでに修正前の値を見ていて、画面にも 1 フレームのあいだその値が表示されるかもしれません。

## 直し方

宣言の時点で範囲に収める（`state n: int = min(n ?? 0, 3)`）か、別のものに追従する（`effect(other) { … }`、`state x(other) = …`）か、読むものを変えるハンドラーの中で書き込みます。

## 誤った例

```asuka
component Counter {
  state n = 0
  effect {
    if n > 3 { n = 3 }
  }
  Button("{n}") { on press { n += 1 } }
}
```

## 正しい例

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { n = min(n + 1, 3) } }
}
```
