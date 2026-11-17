# native-type

原生组件的属性或事件使用了平台视图无法接收的类型。

编译器报告的错误：修正之前程序无法构建。

## 原因

原生组件的属性和事件会以值的形式传给 Swift、Kotlin、C 和 JavaScript。闭包和 store 无法传过去。

## 如何修正

使用平台代码能读取的数据；发生了什么用事件来报告。

## 错误示例

```asuka
native component Dial {
  prop format: fn(int) -> string
}
```

## 正确示例

```asuka
native component Dial {
  prop value: int
  event change(value: int)
}
```
