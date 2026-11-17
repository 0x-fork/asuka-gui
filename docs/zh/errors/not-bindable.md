# not-bindable

`bind` 了一个不报告变化的属性。

编译器报告的错误：修正之前程序无法构建。

## 原因

`bind` 把状态传进去，并在元素报告变化时写回状态：可以绑定控件自己的值（`TextField` 的 `text`、`Toggle` 的 `on`），或者组件声明了 `event <prop>Change` 的 prop。

## 如何修正

绑定控件的值，或者直接传值（`text: s`）并处理它的事件。

## 错误示例

```asuka
component Name {
  state s = ""
  Text(s) { bind text: s }
}
```

## 正确示例

```asuka
component Name {
  state s = ""
  TextField { bind text: s }
}
```
