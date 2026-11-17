# not-assignable

修改了不能修改的东西。

编译器报告的错误：修正之前程序无法构建。

## 原因

能修改的是 `var`、状态，或者它们的字段和元素。`let` 是常量（在组件里则是从它读取的东西派生出来的），参数属于调用方，方法只有声明为 `mut fn` 时才能修改它所在的值。

## 如何修正

把它改成 `var`（或 `state`），修改 `let` 读取的东西，或者把方法声明为 `mut fn`。

## 错误示例

```asuka
fn countdown() -> int {
  let n = 3
  n -= 1
  return n
}
```

## 正确示例

```asuka
fn countdown() -> int {
  var n = 3
  n -= 1
  return n
}
```
