# wide-fanout

One write ran more than 1,000 bindings and lets, more than once.

Reported by the core as the app runs, in development builds (`asuka dev`, `asuka run`, `asuka test`), once per place in the code: release builds don't check it.

## Why

A write runs again what read the state it wrote. A list whose rows each read the whole array runs every row again when one item changes: one row's change costs the whole list, every time.

## How to fix

Read what a row shows by its path (`todos[i].done`, `for t in todos key t.id`), or a set's `contains`, so a write runs only what reads it.

## Wrong

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

## Right

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
