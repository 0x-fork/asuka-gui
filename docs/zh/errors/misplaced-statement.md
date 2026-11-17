# misplaced-statement

语句写在了无法发挥作用的地方。

编译器报告的错误：修正之前程序无法构建。

## 原因

`break` 和 `continue` 用来离开循环，所以要写在循环里；`return` 离开的是函数，而不是 `defer` 或组件的主体；`emit` 触发组件的事件，所以要写在组件里；`let … else` 是函数或处理函数里的语句。

## 如何修正

把语句移到有意义的地方，或者在这里写出你真正想做的事。

## 错误示例

```asuka
fn first(xs: [int]) -> int {
  if xs.isEmpty() { break }
  return xs[0]
}
```

## 正确示例

```asuka
fn first(xs: [int]) -> int {
  if xs.isEmpty() { return 0 }
  return xs[0]
}
```
