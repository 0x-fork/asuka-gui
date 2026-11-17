# let-else

`let … else` 的 else 会继续往下执行，或者根本没有可解包的东西。

编译器报告的错误：修正之前程序无法构建。

## 原因

`let x = optional else { … }` 会在代码块剩下的部分绑定 `x`，所以它的 `else` 不能继续执行到那里，必须离开（`return`、`throw`、`break`、`continue`）。一定有值的值不需要 `else`。

## 如何修正

用 `return`（或 `throw`、`break`、`continue`）结束 `else`，或者去掉它。

## 错误示例

```asuka
fn shown(name: string?) -> string {
  let n = name else { print("no name") }
  return n
}
```

## 正确示例

```asuka
fn shown(name: string?) -> string {
  let n = name else { return "" }
  return n
}
```
