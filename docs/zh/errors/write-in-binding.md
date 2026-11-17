# write-in-binding

在界面跟随的地方写入了状态。

编译器报告的错误：修正之前程序无法构建。

## 原因

绑定、`let` 和组件的主体描述的是始终成立的事实；它们读取的东西一变化，它们就会重新运行。在这里写入状态，每次它引起的变化都会让它再运行一次。

## 如何修正

在处理函数（`on press { … }`）、fn、task 或 effect 里写入状态。由状态计算出来的值用 `let`。

## 错误示例

```asuka
component Counter {
  state n = 0
  let shown = if n > 9 {
    n = 0
    0
  } else {
    n
  }
  Button("{shown}") { on press { n += 1 } }
}
```

## 正确示例

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { n = if n >= 9 { 0 } else { n + 1 } } }
}
```
