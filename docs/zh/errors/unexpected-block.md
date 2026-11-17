# unexpected-block

在不接受代码块的调用后面写了代码块。

编译器报告的错误：修正之前程序无法构建。

## 原因

调用后面的代码块是它的最后一个参数（一个函数），或者是元素的属性和子视图。最后一个参数不是函数的函数，以及 struct、store 和 worker，都只在括号里接收值。

## 如何修正

把值写在括号里，去掉代码块。

## 错误示例

```asuka
fn twice(n: int) -> int = n * 2

let four = twice(2) {
  3
}
```

## 正确示例

```asuka
fn twice(n: int) -> int = n * 2

let four = twice(2)
```
