# wide-fanout

一次写入让 1,000 多个绑定和 let 重新运行，而且不止一次。

应用运行时由核心报告，只在开发构建中检查（`asuka dev`、`asuka run`、`asuka test`），代码里的每个位置只报告一次；发布构建不做这项检查。

## 原因

写入状态时，读取过它的东西都会重新运行。如果列表的每一行都读取整个数组，那么只要有一项变化，每一行都会重新运行：改动一行，每次都要付出整个列表的代价。

## 如何修正

让每行按路径读取它显示的内容（`todos[i].done`、`for t in todos key t.id`），或者使用 set 的 `contains`，这样一次写入只会让读取它的东西重新运行。

## 错误示例

```asuka
component Grid {
  state picked = 0
  Column {
    for i in 0..<1200 {
      Text(if picked == i { "picked {i}" } else { "{i}" })
    }
    Button("Pick") { on press { picked += 1 } }
  }
}

test "every row runs again" {
  let app = mount(Grid())
  app.tap("Pick")
  app.tap("Pick")
  app.expect("picked 2")
}
```

## 正确示例

```asuka
component Cell(i: int, picked: bool) {
  Text(if picked { "picked {i}" } else { "{i}" })
}

component Grid {
  state picked: Set<int> = []
  Column {
    for i in 0..<1200 {
      Cell(i, picked.contains(i))
    }
    Button("Pick") { on press { picked = [picked.count() + 1] } }
  }
}

test "only the rows that change" {
  let app = mount(Grid())
  app.tap("Pick")
  app.tap("Pick")
  app.expect("picked 2")
}
```
