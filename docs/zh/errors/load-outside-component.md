# load-outside-component

在组件或 store 的 `let` 之外使用了 `load`。

编译器报告的错误：修正之前程序无法构建。

## 原因

`load` 在声明它的东西显示期间保存一个异步结果，并在参数变化时重新运行：它属于组件或 store 的 `let`。函数没有这样的生命周期。

## 如何修正

在组件里声明 `let x = load f(…)`，或者在函数里 `await` 这个调用。

## 错误示例

```asuka
async fn fetchName() -> string {
  await sleep(1ms)
  return "Ana"
}

fn name() {
  let n = load fetchName()
}
```

## 正确示例

```asuka
async fn fetchName() -> string {
  await sleep(1ms)
  return "Ana"
}

component Name {
  let n = load fetchName()
  Text(n.value ?? "…")
}
```
