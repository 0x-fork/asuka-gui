# spawn-captures-state

状態や値でないものを読み書きする `spawn`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`spawn` はブロックを別のスレッドで実行し、そのあいだもメインスレッドは状態を変え続けます。受け取るのは開始時にコピーされる値だけで、返すのも値です。

## 直し方

先に状態を `let` に読み込んでから渡し、`spawn` が返したものを代入します。

## 誤った例

```asuka
component Total {
  state items: [int] = []
  state sum = 0
  task {
    sum = await spawn { items.count() }
  }
  Text("{sum}")
}
```

## 正しい例

```asuka
component Total {
  state items: [int] = []
  state sum = 0
  task {
    let xs = items
    sum = await spawn { xs.count() }
  }
  Text("{sum}")
}
```
