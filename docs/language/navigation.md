# Navigation

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

A `screen` is a component whose root is a screen of a stack: its block
takes the screen's props (`title`, `largeTitle`, ...) and slots
(`toolbar`, `bottomToolbar`, `primary`). `push(…)` and `pop()` act on
the nearest stack, `present(…)` and `dismiss()` on sheets. `Split` is a
sidebar of `Destination`s (in `Sidebar` sections) beside the chosen
one's stack.

With a `Router`, every screen has an address. A screen declared with
its pattern is routable, its parameters typed; a call of it is its
address, checked as the app compiles:

```asuka check
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

See [Navigation](../navigation.md).
