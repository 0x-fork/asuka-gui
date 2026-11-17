# effect-writes-what-it-follows

effect 写入了它读取的状态。

编译器报告的错误：修正之前程序无法构建。

## 原因

effect 读取的东西变化时会再次运行。如果它写的正是这个东西，它就会在自己写入之后再运行一次，晚了一轮：处理函数已经看到了它要修正的值，屏幕也可能把这个值显示上一帧。

## 如何修正

在声明处把值限制在范围内（`state n: int = min(n ?? 0, 3)`），改为跟随别的东西（`effect(other) { … }`、`state x(other) = …`），或者在修改它所读内容的处理函数里写入。

## 错误示例

```asuka
component Counter {
  state n = 0
  effect {
    if n > 3 { n = 3 }
  }
  Button("{n}") { on press { n += 1 } }
}
```

## 正确示例

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { n = min(n + 1, 3) } }
}
```
