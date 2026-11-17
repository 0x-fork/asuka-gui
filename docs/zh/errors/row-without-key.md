# row-without-key

视图里的 `for` 遍历会变化的数据，却没有 `key`。

编译器报告的错误：修正之前程序无法构建。

## 原因

没有 key 时，行按位置保留：某一项移动后，原来位置上的行会显示下一项，却还带着原来那一行的状态、task 和视图。

## 如何修正

写出用什么标识一项：`for t in todos key t.id`。如果就是想按位置，写 `key i`。

## 错误示例

```asuka
struct Todo {
  id: int
  title: string
}

component Todos {
  state todos: [Todo] = []
  Column {
    for t in todos { Text(t.title) }
  }
}
```

## 正确示例

```asuka
struct Todo {
  id: int
  title: string
}

component Todos {
  state todos: [Todo] = []
  Column {
    for t in todos key t.id { Text(t.title) }
  }
}
```
