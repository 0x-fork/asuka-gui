# async-in-effect

エフェクトの中にある `await`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

エフェクトはすぐに実行され、読んだものが変わるたびに再び実行されます。待つ実行があると、次の実行が始まるときにまだ動いているかもしれません。非同期の処理は、前の実行をキャンセルしてから再開するタスクの役目です。

## 直し方

`task(what) { … }` を使います。コンポーネントが現れたときに実行され、`what` が変わると前の実行をキャンセルして再び実行されます。

## 誤った例

```asuka
async fn save(n: int) {
  await sleep(1s)
}

component Counter {
  state n = 0
  effect { await save(n) }
  Button("{n}") { on press { n += 1 } }
}
```

## 正しい例

```asuka
async fn save(n: int) {
  await sleep(1s)
}

component Counter {
  state n = 0
  task(n) { await save(n) }
  Button("{n}") { on press { n += 1 } }
}
```
