# invalid-pattern

写法不被语言接受的模式。

编译器报告的错误：修正之前程序无法构建。

## 原因

模式有固定的写法：case 写成 `.name(…)` 或 `Type.name(…)`，struct 模式要写出字段名（`Point(x: 0)`），`|` 连接的模式不能绑定名称，`let` 只绑定名称而不做判断，`for` 绑定一个名称或一个名称元组。

## 如何修正

按消息给出的写法来写：`.case(x)`、`if let`、`(k, v)`。

## 错误示例

```asuka
enum Shape {
  circle(radius: float)
  square(side: float)
}

fn size(s: Shape) -> float = match s {
  .circle(x) | .square(x) => x
}
```

## 正确示例

```asuka
enum Shape {
  circle(radius: float)
  square(side: float)
}

fn size(s: Shape) -> float = match s {
  .circle(r) => r
  .square(side) => side
}
```
