# recursive-type

包含自身完整副本的类型。

编译器报告的错误：修正之前程序无法构建。

## 原因

struct 的字段都在它内部，所以字段类型是自身的 struct 会无限大，根本造不出这样的值。类型别名不能引用自己，C 的 struct 也不能包含自己。

## 如何修正

把路径上的某个字段改成可选（`next: Node?`）或数组（`children: [Node]`）。

## 错误示例

```asuka
struct Node {
  value: int
  next: Node
}
```

## 正确示例

```asuka
struct Node {
  value: int
  next: Node? = none
}
```
