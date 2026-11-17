# await-outside-async

在不能等待的地方写了 `await`。

编译器报告的错误：修正之前程序无法构建。

## 原因

只有 async 函数、处理函数、task 和测试可以等待。普通函数会一口气运行到结束，组件的主体是声明，而不是会运行的代码。

## 如何修正

把函数标记为 `async`；在组件里则用 `let x = load f()` 或 `task { }` 加载数据。

## 错误示例

```asuka
async fn fetchCount() -> int {
  await sleep(1s)
  return 1
}

fn total() -> int {
  return await fetchCount()
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
