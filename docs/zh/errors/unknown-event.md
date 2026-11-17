# unknown-event

处理或 `emit` 了组件没有声明的事件。

编译器报告的错误：修正之前程序无法构建。

## 原因

`on name { }` 处理元素报告的事件，`emit name` 触发所在组件声明的事件（`event name`）。不存在的事件永远不会触发。

## 如何修正

使用它已有的事件（消息会给出建议），或者声明它：`event name(…)`。

## 错误示例

```asuka
component Go {
  Button("Go") { on tap { print("go") } }
}
```

## 正确示例

```asuka
component Go {
  Button("Go") { on press { print("go") } }
}
```
