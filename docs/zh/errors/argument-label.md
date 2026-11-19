# argument-label

具名参数在函数里没有对应的形参。

编译器报告的错误：修正之前程序无法构建。

## 原因

参数先按顺序给出，再按名字给出：名字必须是某个形参的名字，只能出现一次，并且写在按顺序给出的参数之后。闭包的参数没有名字，大多数内置函数的参数也没有（print 的 `separator:` 和 `terminator:` 例外）。

## 如何修正

使用形参的名字（消息会给出建议），或者按顺序传参。

## 错误示例

```asuka
fn area(width: int, height: int) -> int = width * height

let a = area(width: 2, hieght: 3)
```

## 正确示例

```asuka
fn area(width: int, height: int) -> int = width * height

let a = area(width: 2, height: 3)
```
