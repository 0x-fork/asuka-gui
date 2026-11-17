# not-a-value

在需要值的地方用了类型、包或方法。

编译器报告的错误：修正之前程序无法构建。

## 原因

类型是一类值的名字，包是一组声明，方法需要调用它的那个值：它们都不是可以传递或保存的值。

## 如何修正

创建这个类型的值（`Point(x: 1, y: 2)`），使用包的成员，或者调用这个方法。

## 错误示例

```asuka
struct Point {
  x: int
  y: int
}

let origin = Point
```

## 正确示例

```asuka
struct Point {
  x: int
  y: int
}

let origin = Point(x: 0, y: 0)
```
