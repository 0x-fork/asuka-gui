# fn-body

函数缺少函数体，或者有了不该有的函数体。

编译器报告的错误：修正之前程序无法构建。

## 原因

函数都有函数体，除非由平台来实现：native interface 的方法、native fn 和 extern fn（由 C 实现）都没有函数体。

## 如何修正

写出函数体（`{ … }` 或 `= 表达式`），或者从 native、extern 声明里去掉它。

## 错误示例

```asuka
fn greeting() -> string
```

## 正确示例

```asuka
fn greeting() -> string = "Hello"
```
