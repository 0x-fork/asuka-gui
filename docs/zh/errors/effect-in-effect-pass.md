# effect-in-effect-pass

effect 的写入让一个绑定在一次刷新里运行了两次。

应用运行时由核心报告，只在开发构建中检查（`asuka dev`、`asuka run`、`asuka test`），代码里的每个位置只报告一次；发布构建不做这项检查。

## 原因

在一次刷新里，绑定先于 effect 运行。effect 写入了绑定读取的东西，绑定就会在同一次刷新里再运行一次：画面已经用旧值布局过一次，白白浪费了。

## 如何修正

用 `let` 或跟随 effect 所读内容的状态来计算这个值：它一轮就能稳定下来。

## 错误示例

```asuka
component Label {
  state name = "ana"
  state shown = ""
  effect { shown = name.upper() }
  Column {
    Text(shown)
    Button("Rename") { on press { name = "bo" } }
  }
}

test "a pass late" {
  let app = mount(Label())
  app.tap("Rename")
  app.expect("BO")
}
```

## 正确示例

```asuka
component Label {
  state name = "ana"
  let shown = name.upper()
  Column {
    Text(shown)
    Button("Rename") { on press { name = "bo" } }
  }
}

test "in one pass" {
  let app = mount(Label())
  app.tap("Rename")
  app.expect("BO")
}
```
