# not-async-call

`load`、`all`、`race` に async 呼び出しではないものを渡している。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`load f(…)` は非同期の結果を保持し、`all` と `race` は async 呼び出しを同時に実行します。ふつうの値には待つものがありません。

## 直し方

async 呼び出しを渡す（`load api.user(id)`）か、値をそのまま使います。

## 誤った例

```asuka
fn one() -> int = 1

async fn both() -> int {
  let (a, b) = await all(one(), one())
  return a + b
}
```

## 正しい例

```asuka
async fn one() -> int {
  await sleep(1ms)
  return 1
}

async fn both() -> int {
  let (a, b) = await all(one(), one())
  return a + b
}
```
