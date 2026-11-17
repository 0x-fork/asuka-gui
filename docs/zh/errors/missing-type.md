# missing-type

必须写出类型的声明。

编译器报告的错误：修正之前程序无法构建。

## 原因

参数要写出类型，prop 要写出类型或者给一个可以推断类型的默认值。读取自身（当前值，第一次是 `none`）的状态，无法从它自己的值推断类型。

## 如何修正

写出类型：`prop size: int`、`prop size = 12`、`state n: int = …`。

## 错误示例

```asuka
component Label {
  prop size
  Text("{size}")
}
```

## 正确示例

```asuka
component Label {
  prop size = 12
  Text("{size}")
}
```
