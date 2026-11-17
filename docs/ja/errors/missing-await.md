# missing-await

async 関数を `await` なしで呼び出している。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

async 関数の結果はあとで届きます。`await` はコードがどこで待つかを示すので、待っているあいだにほかのハンドラーが動きうる場所がすべて書き表されます。

## 直し方

`await f(…)` と書く（throw するなら `try await`）か、`spawn` で単独で開始します。

## 誤った例

```asuka
async fn fetchCount() -> int {
  await sleep(1s)
  return 1
}

async fn total() -> int {
  return fetchCount()
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
