# ストア

ストアは、状態とそれを変更する関数をまとめたもので、それを使うビューのあいだで共有されます。

```asuka
pub store Mailbox {
  state messages: [Message] = []
  let unread = messages.count(m => !m.read)

  pub fn markRead(id: string) {
    if let i = messages.index(m => m.id == id) { messages[i].read = true }
  }

  pub async fn refresh() throws {
    messages = try await http.get("https://example.com/inbox").json()
  }
}
```

ストアは構造体と同じように作り（`Mailbox()`）、作ったコンポーネントまたはストアのものになります。`provide` でその下にあるすべてから使えるようにし、`use` で取り出します。

```asuka
app {
  provide Mailbox()
  Stack { Inbox() }
}

screen Inbox {
  use mailbox: Mailbox
  …
}
```

ファイルの最上位に書いた `state` はアプリのもので、アプリ全体で一つだけです。

ストアは構造体と同じように型パラメータを取れます。型は引数か、期待される型から推論されます。

```asuka
store Kept<T>(key: string, initial: T) {
  pub state value: T = initial
}

let size = Kept("size", 17.0)                 // Kept<float>
let tags: Kept<[string]> = Kept("tags", [])
```

ストアの fn の中では、`self` はそのストア自身です。ストアが作るものに渡せます。
