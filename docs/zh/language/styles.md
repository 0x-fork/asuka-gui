# 样式

每个元素都接收样式 prop：先是布局（`direction`、`justify`、`align`、`gap`、`padding`、`margin`、`width`、`height`、`grow`、`shrink`、`absolute`、`top`、`grid` ……），然后是外观（`background`、`radius`、`border`、`opacity`、`shadow`、`font`、`color`、`lines` ……）。详见[布局与样式](../layout.md)。

```asuka
style card {
  padding: 16; radius: 12; gap: 8
  background: .secondaryBackground
}

View { style card; background: .accent }   // 后写的 prop 优先
```

`pub style` 也能在别的包里使用：写作 `style theme.card`，其中的值在声明它的包里求值。

不设置任何样式的元素，看起来就和平台自己的一样。

容器（`View`、`Row`、`Column`、`SafeArea`）可以写**变体**：以交互状态命名的一组样式 prop，在容器处于该状态时叠加到它的样式上，相当于 CSS 的 `:hover` 和 `:active`。状态有四种：`hovered`、`pressed`、`dragging`（正被拖动时）和 `dragOver`（它接受的拖动正停在它上方时），按这个顺序叠加，所以 `pressed` 会盖过 `hovered`。变体只覆盖它写出的 prop，它的值和普通 prop 一样是绑定：

```asuka
View {
  padding: 16; radius: 12; background: .secondaryBackground
  hovered { borderColor: .accent }
  pressed { background: .separator; opacity: 0.8 }
  on press { open() }
}
```

只有当某处用到了某个状态，视图才会上报它。因此没有写 `pressed` 变体的视图，仍然保留平台自己的按下效果。变体里也可以用 `style`（`pressed { style raised }`）。其他视图可以通过名字跟随一个视图的状态（见[给视图命名](#给视图命名)）：写成 `View as card { … }`，然后在任意绑定里读 `card.hovered`（或 `card.pressed`、`card.dragging`、`card.dragOver`），子视图可以这样做（整组悬停），兄弟视图也可以。与样式无关的反应则交给处理器：`on pressedChange(p)`、`on hoverChange(h)`。

## 给视图命名

有些事不是给视图设置一个值，而是要对视图做点什么：播放视频、在网页里运行脚本、往输入框里插入文本、滚动列表。`Ref` 就是视图的句柄：在任意元素上写 `ref: r`，它的视图就放进 `r`；写成 `Element as name`，则会为所在代码块的剩余部分声明一个这样的句柄。

```asuka
component Player {
  let video = Ref()

  Video("intro.mp4") { ref: video }
  Button("Start over") {
    on press {
      video.seek(0)
      video.play()
    }
  }
}
```

只有视图显示期间，Ref 才持有它；在那之前和之后都是空的，这时命令什么也不做，状态都是 false。Ref 的状态会被追踪，读取它的绑定会跟着它变化：

| 适用于 | Ref 的成员 |
|---|---|
| 任意视图 | `hovered`、`pressed`、`dragging`、`dragOver`（bool，会被追踪）；`focus()`、`blur()` |
| `Scroll`、`List` | `scrollTo(x, y, animated: true)` |
| `Video` | `play()`、`pause()`、`seek(seconds)` |
| `WebView` | `try await eval(script)`（脚本的值，以 JSON 表示）、`run(script)`（不等待结果）、`goBack()`、`goForward()`、`reload()`、`stopLoading()` |
| `TextField` | `insert(text)`、`format(.bold, on)`、`toggleFormat(.italic)`、`setLink(url)`（作用于富文本输入框的选区） |

Ref 可以传来传去（`shareFiles(paths, from: button)`、`a11yFocus(field)`），也可以放在文件顶层的 `let` 里，供多个组件的视图使用。
