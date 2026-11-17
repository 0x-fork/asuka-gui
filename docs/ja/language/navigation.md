# ナビゲーション

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

`screen` は、ルートがスタックの画面になるコンポーネントです。ブロックには画面のプロパティ（`title`、`largeTitle` など）とスロット（`toolbar`、`bottomToolbar`、`primary`）を書きます。`push(…)` と `pop()` はいちばん近いスタックに、`present(…)` と `dismiss()` はシートに対して働きます。`Split` は、`Destination`（`Sidebar` のセクションに入れられます）を並べたサイドバーと、その横の選んだ行き先のスタックです。

`Router` を使うと、どの画面にもアドレスが付きます。パターン付きで宣言した画面はルートに載せられ、パラメーターには型があります。その呼び出しがアドレスになり、コンパイル時に検査されます。

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

詳しくは[ナビゲーション](../navigation.md)を参照してください。
