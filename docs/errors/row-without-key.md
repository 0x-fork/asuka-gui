# row-without-key

A `for` in a view over items that change, without a `key`.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Without a key, rows are kept by position: when an item moves, the row at its old place shows the next item, with the state, tasks and views of the row that was there.

## How to fix

Say what identifies an item: `for t in todos key t.id`. `key i` keeps the position on purpose.

## Wrong

```asuka
struct Todo {
  id: int
  title: string
}

component Todos {
  state todos: [Todo] = []
  Column {
    for t in todos { Text(t.title) }
  }
}
```

## Right

```asuka
struct Todo {
  id: int
  title: string
}

component Todos {
  state todos: [Todo] = []
  Column {
    for t in todos key t.id { Text(t.title) }
  }
}
```
