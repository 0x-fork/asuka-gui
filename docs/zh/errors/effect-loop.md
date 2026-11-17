# effect-loop

在一次刷新（flush）里不断互相弄脏的 effect。

应用运行时核心触发的 panic，任何构建都会检查：正在运行的部分会停止，崩溃报告里会带上这个代码。

## 原因

写入自己（或另一个 effect）读取的东西的 effect 会再次运行。如果这些写入永远不会稳定下来，刷新就永远不会结束：运行 100 轮之后，核心会停下它，并指出是哪个 effect、在哪一行，以及它最后写入的状态。编译器能报告它看得见的循环（effect-writes-what-it-follows），而这是它看不见的那种：通过函数值完成的写入。

## 如何修正

让这个值只有一个来源：用 `let` 或跟随状态计算它，或者在修改 effect 所读内容的处理函数里写入它。

## 错误示例

```asuka
store Looper {
  state runs = 0
  state step: fn() = () => {}

  effect {
    let _ = runs
    step()
  }
}

test "a loop" {
  let l = Looper()
  l.step = () => { l.runs += 1 }
}
```

## 正确示例

```asuka
store Looper {
  state runs = 0
  state step: fn() = () => {}

  effect(step) {
    step()
  }
}

test "no loop" {
  let l = Looper()
  l.step = () => { l.runs += 1 }
  expect(l.runs == 1)
}
```
