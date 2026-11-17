# await-outside-async

待つことのできない場所にある `await`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

待てるのは async 関数、ハンドラー、タスク、テストだけです。ふつうの関数は最後まで一気に実行され、コンポーネントの本体は宣言であって、実行されるコードではありません。

## 直し方

関数に `async` を付けます。コンポーネントでは `let x = load f()` か `task { }` でデータを読み込みます。

## 誤った例

```asuka
async fn fetchCount() -> int {
  await sleep(1s)
  return 1
}

fn total() -> int {
  return await fetchCount()
}
```

## 正しい例

```asuka
async fn fetchCount() -> int {
  await sleep(1s)
  return 1
}

async fn total() -> int {
  return await fetchCount()
}
```
