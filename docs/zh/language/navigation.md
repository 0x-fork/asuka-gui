# 导航

```asuka
app {
  Tabs {
    Tab("Inbox") { icon: "tray"; Stack { Inbox() } }
    Tab("Settings") { icon: "gear"; Stack { Settings() } }
  }
}

screen Inbox {
  title: "Inbox"
  List {
    for m in messages key m.id {
      MessageRow(m) { on press { push(Thread(m.id)) } }
    }
  }
  toolbar {
    Button("Compose") { icon: "square.and.pencil"; on press { present(Compose()) } }
  }
}
```

`screen` 是一种组件，它的根是导航栈中的一个页面：它的代码块接收页面的 prop（`title`、`largeTitle` ……）和插槽（`toolbar`、`bottomToolbar`、`primary`）。`push(…)` 和 `pop()` 作用于最近的导航栈，`present(…)` 和 `dismiss()` 作用于弹出面板（sheet）。`Split` 是一个由 `Destination` 组成的侧边栏（可以放在 `Sidebar` 分区里），旁边是选中目的地的导航栈。

有了 `Router`，每个页面都有一个地址。用模式声明的页面是可路由的，参数有类型；调用它就得到它的地址，并在编译时检查：

```asuka
app {
  Router(initial: "/inbox") {
    Route("/inbox", layout: .stack, title: "Inbox") {
      Inbox
      Message
    }
  }
}

screen Inbox at "" {
  title: "Inbox"
  Button("The first") { href: Message(id: 1) } // /inbox/1
}

screen Message(id: int) at ":id" {
  title: "Message {id}"
  Button("Back to the inbox") { on press { navigate("/inbox") } }
}
```

详见[导航](../navigation.md)。
