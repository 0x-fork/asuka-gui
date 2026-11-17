# not-async-call

传给 `load`、`all` 或 `race` 的不是 async 调用。

编译器报告的错误：修正之前程序无法构建。

## 原因

`load f(…)` 保存一个异步结果，`all` 和 `race` 同时运行多个 async 调用。普通的值没有什么可等待的。

## 如何修正

传入一个 async 调用（`load api.user(id)`），或者直接使用这个值。

## 错误示例

```asuka
fn one() -> int = 1

async fn both() -> int {
  let (a, b) = await all(one(), one())
  return a + b
}
```

## 正确示例

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
