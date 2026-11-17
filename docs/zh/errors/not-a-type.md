# not-a-type

当作类型使用、实际上却不是类型的东西。

编译器报告的错误：修正之前程序无法构建。

## 原因

在需要类型的地方（参数、字段、`:` 之后），名称必须是类型：内置类型、struct、enum、interface 或类型别名。`void?` 和 `T??` 也不是类型。

## 如何修正

写出你想要的类型：`void?` 应写成 `bool`，可选类型只要一个 `?`。

## 错误示例

```asuka
fn shown(flag: void?) -> bool = flag != none
```

## 正确示例

```asuka
fn shown(flag: bool) -> bool = flag
```
