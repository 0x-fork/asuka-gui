# statement-in-view

组件的视图之间写了语句。

编译器报告的错误：修正之前程序无法构建。

## 原因

组件的主体是声明：状态、let，以及始终成立的视图。写在这里的语句只会运行一次，或者运行时机不明确；视图里的 match 也不能带守卫条件。

## 如何修正

把语句放进处理函数（`on press { … }`）、fn、task 或 effect；派生的值用 `let`。

## 错误示例

```asuka
component Counter {
  state n = 0
  Column {
    n += 1
    Text("{n}")
  }
}
```

## 正确示例

```asuka
component Counter {
  state n = 0
  Column {
    Button("Add") { on press { n += 1 } }
    Text("{n}")
  }
}
```
