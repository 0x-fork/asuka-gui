# constraint-not-met

类型缺少泛型或方法对它的要求。

编译器报告的错误：修正之前程序无法构建。

## 原因

set 的元素和 map 的键必须是 `Hashable`，`==` 需要 `Equatable` 的值，`sorted()` 需要 `Comparable` 的值，泛型的类型参数也会写明要求（`T: Comparable`）。struct 的字段都满足时，struct 才满足；闭包则一个也不满足。

## 如何修正

换成满足要求的类型（比如用 id 代替整个值），或者给类型补上缺少的部分。

## 错误示例

```asuka
struct Handler {
  run: fn()
}

let seen = Set<Handler>()
```

## 正确示例

```asuka
struct Handler {
  id: int
}

let seen = Set<Handler>()
```
