# missing-await

调用 async 函数时没有写 `await`。

编译器报告的错误：修正之前程序无法构建。

## 原因

async 函数的结果要稍后才能得到；`await` 标出代码在哪里等待它，这样每一个其他处理函数可能插进来运行的地方都写得清清楚楚。

## 如何修正

写成 `await f(…)`（会抛错时写 `try await`），或者用 `spawn` 单独启动它。

## 错误示例

```asuka
async fn fetchCount() -> int {
  await sleep(1s)
  return 1
}

async fn total() -> int {
  return fetchCount()
}
```

## 正确示例

```asuka
async fn fetchCount() -> int {
  await sleep(1s)
  return 1
}

async fn total() -> int {
  return await fetchCount()
}
```
