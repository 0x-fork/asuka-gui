# view-in-statement

在会运行的代码里创建视图。

编译器报告的错误：修正之前程序无法构建。

## 原因

视图声明在组件的主体里，只要决定它的条件成立，它就一直存在。在处理函数或函数里创建的视图会创建后立刻丢弃，哪里也不会显示。

## 如何修正

在主体里声明这个视图，放在一个由处理函数设置的状态控制的 `if` 下面。

## 错误示例

```asuka
component Hello {
  Button("Greet") {
    on press { Text("Hi!") }
  }
}
```

## 正确示例

```asuka
component Hello {
  state greeted = false
  Column {
    Button("Greet") { on press { greeted = true } }
    if greeted { Text("Hi!") }
  }
}
```
