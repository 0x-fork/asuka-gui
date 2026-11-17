# invalid-conformance

类型声明的、或类型参数要求的东西不是 interface。

编译器报告的错误：修正之前程序无法构建。

## 原因

类型在 `:` 后面写的是 interface：`Error`、`Equatable`、`Comparable`、`Hashable`，或者程序自己声明的 interface。native interface 由平台实现，类型不能声明遵循它。

## 如何修正

在那里写一个 interface，或者去掉它。

## 错误示例

```asuka
struct Score: int {
  points: int
}
```

## 正确示例

```asuka
struct Score: Equatable {
  points: int
}
```
