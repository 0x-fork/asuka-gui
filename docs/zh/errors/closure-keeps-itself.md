# closure-keeps-itself

闭包通过保存它的变量调用自己。

编译器报告的警告：程序仍能构建，但值得看一看。

## 原因

闭包持有这个变量，变量又持有这个闭包，形成了引用循环，永远不会被释放。

## 如何修正

改为在代码块里声明函数 `fn name(…) { … }`：它可以调用自己。

## 错误示例

```asuka
fn countdown(n: int) -> int {
  var step: fn(int) -> int = x => x
  step = x => if x <= 0 { 0 } else { step(x - 1) }
  return step(n)
}
```

## 正确示例

```asuka
fn countdown(n: int) -> int {
  fn step(x: int) -> int = if x <= 0 { 0 } else { step(x - 1) }
  return step(n)
}
```
