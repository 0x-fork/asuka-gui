# unknown-prop

元素没有这个属性、插槽或变体。

编译器报告的错误：修正之前程序无法构建。

## 原因

元素只接受它声明的属性；如果它显示的视图接受样式属性，也接受样式属性。样式里只能写样式属性。拼错的属性如果不报错，就会悄无声息地什么也不做。

## 如何修正

使用它已有的属性（消息会给出建议）。

## 错误示例

```asuka
component Title {
  Text("Hello") { colour: .red }
}
```

## 正确示例

```asuka
component Title {
  Text("Hello") { color: .red }
}
```
