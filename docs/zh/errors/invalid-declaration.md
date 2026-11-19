# invalid-declaration

声明的写法不被它所在的位置接受。

编译器报告的错误：修正之前程序无法构建。

## 原因

类型的常量写成 `let name = value`，它不会改变；泛型类型的常量是它所有类型共用的一个，所以它的类型不能提到类型参数。组件的 `let` 只能命名一个值，默认值也不能读取还没创建出来的字段。

## 如何修正

按这个位置的写法来写：类型的常量用 `let name = value`，需要类型参数的就写成 `static fn`。

## 错误示例

```asuka
struct Money {
  cents: int

  var zero = 0
}
```

## 正确示例

```asuka
struct Money {
  cents: int

  let zero = 0
}
```
