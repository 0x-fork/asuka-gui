# peek-untracked

在不跟随任何东西的地方使用了 `peek`。

编译器报告的错误：修正之前程序无法构建。

## 原因

`peek(x)` 在会被跟随的地方读取 `x` 而不跟随它：绑定、`let`、状态的值。处理函数、task、测试和 `effect(…)` 的主体本来就不跟随任何东西，在那里写 `peek` 没有意义。

## 如何修正

直接读取这个值。

## 错误示例

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { print(peek(n)) } }
}
```

## 正确示例

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { print(n) } }
}
```
