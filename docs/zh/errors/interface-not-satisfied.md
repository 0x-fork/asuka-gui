# interface-not-satisfied

类型声明了某个 interface，却没有实现它。

编译器报告的错误：修正之前程序无法构建。

## 原因

`struct T: Shape` 承诺实现 `Shape` 列出的每个方法，并且签名一致，这样就可以在不知道具体类型的情况下通过 `Shape` 调用。

## 如何修正

按 interface 的签名补上缺少的方法（消息里列出了它们）。

## 错误示例

```asuka
interface Shape {
  fn area() -> float
}

struct Square: Shape {
  side: float
}
```

## 正确示例

```asuka
interface Shape {
  fn area() -> float
}

struct Square: Shape {
  side: float
  fn area() -> float = side * side
}
```
