# mixed-numbers

运算符两边是不同类型的数字。

编译器报告的错误：修正之前程序无法构建。

## 原因

Asuka 不会自动转换数字：`int` 和 `float`，或者 `u8` 和 `int`，不能直接放在 `+`、`<`、`&` 的两边。悄悄把一边加宽，会掩盖数值可能被截断、回绕或丢掉小数的地方。整数字面量会采用另一边的类型，所以 `level + 1` 没问题。

## 如何修正

把一边转换成另一边的类型：`int(level) + offset`、`float(n) * 0.5`、`u8(n)`（n 放不下时抛出错误；`u8(truncating: n)` 只保留低位）。

## 错误示例

```asuka
fn total(level: u8, bonus: int) -> int = level + bonus
```

## 正确示例

```asuka
fn total(level: u8, bonus: int) -> int = int(level) + bonus
```
