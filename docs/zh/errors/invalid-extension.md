# invalid-extension

`extend` 了不能扩展的东西，或者写法不对。

编译器报告的错误：修正之前程序无法构建。

## 原因

扩展只能给有名字的类型（内置的或声明的）添加成员，不能扩展函数类型、元组或 interface。泛型扩展的类型参数就是类型自己的，常量和 static fn 也需要一个有名字的类型来调用。

## 如何修正

扩展一个有名字的类型，或者改为声明一个函数。

## 错误示例

```asuka
extend fn() -> int {
  fn twice() -> int = self() * 2
}
```

## 正确示例

```asuka
fn twice(f: fn() -> int) -> int = f() * 2
```
