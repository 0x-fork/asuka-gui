# type-arguments

类型参数的个数不对。

编译器报告的错误：修正之前程序无法构建。

## 原因

泛型类型或函数规定了它接受几个类型：`Set<T>` 一个，`[K: V]` 两个，struct 则按它自己的声明。不是泛型的类型不接受类型参数。

## 如何修正

按声明给出相同个数的类型：`Set<int>`、`Page<Item>`。

## 错误示例

```asuka
struct Box<T> {
  value: T
}

fn unbox(b: Box<int, string>) -> int = 0
```

## 正确示例

```asuka
struct Box<T> {
  value: T
}

fn unbox(b: Box<int>) -> int = b.value
```
