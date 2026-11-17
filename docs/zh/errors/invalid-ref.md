# invalid-ref

`ref` 或 `as` 指向的不是单个元素的视图。

编译器报告的错误：修正之前程序无法构建。

## 原因

`ref: x` 和 `as x` 用来命名元素的平台视图，供需要读取它的地方使用（它的状态、滚动）。显示多个视图的组件没有可以命名的单一视图。

## 如何修正

给组件内部的某个元素命名，或者让组件只显示一个元素。

## 错误示例

```asuka
component Pair {
  Text("a")
  Text("b")
}

component Page {
  Pair() as p
}
```

## 正确示例

```asuka
component Pair {
  Row {
    Text("a")
    Text("b")
  }
}

component Page {
  Pair() as p
}
```
