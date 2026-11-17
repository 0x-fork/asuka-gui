# invalid-variant

变体的声明或用法不可行。

编译器报告的错误：修正之前程序无法构建。

## 原因

变体在条件成立期间叠加在组件显示的那个唯一元素上；它包含的是随之变化的样式属性（不包括会挪动周围视图的布局），里面也不能再嵌套变体。

## 如何修正

让组件只显示一个元素（把视图放进 `Row` 或 `Column`），在变体里只设置随它变化的属性。

## 错误示例

```asuka
component Chip {
  prop selected = false
  variant selected = selected
  Text("a")
  Text("b")
}
```

## 正确示例

```asuka
component Chip {
  prop selected = false
  variant selected = selected
  Row {
    Text("a")
    Text("b")
  }
}
```
