# not-supported

语言暂时还不支持的写法。

编译器报告的错误：修正之前程序无法构建。

## 原因

有几种写法能够解析，但还没有实现：泛型组件、缺少所需元素的 screen，以及编译器暂时还无法编译的少数表达式。消息会说明可以改用什么。

## 如何修正

改用消息里给出的替代写法：泛型组件可以改为接收 `View` 或闭包作为 prop。

## 错误示例

```asuka
component Box<T>(value: T) {
  Text("box")
}
```

## 正确示例

```asuka
component Box(value: string) {
  Text(value)
}
```
