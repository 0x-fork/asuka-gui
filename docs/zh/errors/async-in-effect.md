# async-in-effect

在 effect 里写了 `await`。

编译器报告的错误：修正之前程序无法构建。

## 原因

effect 会立即运行，并在它读取的东西变化时再次运行；如果一次运行要等待，下一次开始时它可能还没结束。异步工作应该交给 task，它会先取消上一次运行再重新开始。

## 如何修正

改用 `task(what) { … }`：它在组件出现时运行，`what` 变化时取消上一次运行并再次运行。

## 错误示例

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

## 正确示例

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
