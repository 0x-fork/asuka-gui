# invalid-style

样式的声明或应用方式不被接受。

编译器报告的错误：修正之前程序无法构建。

## 原因

样式里可以写样式属性、`style:`、`use` 和变体；`style:` 只能设置一次，值是一个样式或样式列表（`style: [card, pink]`），列表里是样式，或者用 `if`、`match` 选择样式。

## 如何修正

把样式写进同一个列表：`style: [a, b]`。

## 错误示例

```asuka
style big {
  font: .title
}

style red {
  color: .red
}

component Title {
  Text("Hi") {
    style: big
    style: red
  }
}
```

## 正确示例

```asuka
style big {
  font: .title
}

style red {
  color: .red
}

component Title {
  Text("Hi") {
    style: [big, red]
  }
}
```
