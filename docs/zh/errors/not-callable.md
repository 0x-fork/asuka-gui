# not-callable

调用了不是函数的东西。

编译器报告的错误：修正之前程序无法构建。

## 原因

只有函数、闭包和方法可以调用。普通的值和字段不能调用，interface、enum 和 C 类型也不能通过调用名称来创建。

## 如何修正

调用一个函数，直接使用这个值，或者创建枚举的某个 case（`Kind.name`）。

## 错误示例

```asuka
fn twice(n: int) -> int = n(2)
```

## 正确示例

```asuka
fn twice(n: int) -> int = n * 2
```
