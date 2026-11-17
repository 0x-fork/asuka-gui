# peek-untracked

何も追従しない場所での `peek`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`peek(x)` は、追従される場所（バインディング、`let`、状態の値）で `x` を追従せずに読みます。ハンドラー、タスク、テスト、`effect(…)` の本体はもともと何にも追従しないので、そこでの `peek` には意味がありません。

## 直し方

値そのものを読みます。

## 誤った例

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { print(peek(n)) } }
}
```

## 正しい例

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { print(n) } }
}
```
