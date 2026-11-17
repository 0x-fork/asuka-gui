# not-a-view

视图之间出现了不是视图的值。

编译器报告的错误：修正之前程序无法构建。

## 原因

主体和元素的代码块里只能显示元素和组件。数字或字符串要交给显示文本的元素来显示。

## 如何修正

放进元素里显示：`Text("{n}")`。

## 错误示例

```asuka
component Answer {
  Column {
    42
  }
}
```

## 正确示例

```asuka
component Answer {
  Column {
    Text("42")
  }
}
```
