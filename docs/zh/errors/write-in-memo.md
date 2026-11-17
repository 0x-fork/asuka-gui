# write-in-memo

在计算 `let` 时写入了状态。

应用运行时核心触发的 panic，任何构建都会检查：正在运行的部分会停止，崩溃报告里会带上这个代码。

## 原因

`let` 在被读取时计算，它读取的东西变化多少次就计算多少次；在这里写入状态，会在别人读取的过程中改变他们读取的东西。编译器会报告它看得见的写入（write-in-binding），而这次写入来自函数值。

## 如何修正

在处理函数、fn、task 或 effect 里写入状态；`let` 只读取。

## 错误示例

```asuka
store Counter {
  state n = 0
  state bump: fn() -> int = () => 0
  let shown = bump()
}

test "a write in a let" {
  let c = Counter()
  c.bump = () => {
    c.n += 1
    return c.n
  }
  print(c.shown)
}
```

## 正确示例

```asuka
store Counter {
  state n = 0
  let shown = n + 1
}

test "a let that reads" {
  let c = Counter()
  c.n += 1
  expect(c.shown == 2)
}
```
