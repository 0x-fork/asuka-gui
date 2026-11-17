# bad-operand

运算符用在了它不接受的值上。

编译器报告的错误：修正之前程序无法构建。

## 原因

每个运算符都只接受特定的类型：算术运算要求同一种数字（`int` 配 `int`，`float` 配 `float`），位运算要求整数，`[]` 用于数组、map 和 bytes，`==` 要求两边类型相同。struct 可以通过方法获得运算符（`fn add` 就是 `+`）。

## 如何修正

转换其中一边（`float(n)`），改用方法（`a.concat(b)`），或者给类型加上这个运算符对应的方法。

## 错误示例

```asuka
let rest = "hello" - "h"
```

## 正确示例

```asuka
let rest = "hello".slice(1, 5)
```
