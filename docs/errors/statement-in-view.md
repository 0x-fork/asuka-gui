# statement-in-view

A statement among a component's views.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A component's body is declarations: state, lets, and views that stay true. A statement there would run once, or at no clear time; a match in a view takes no guards.

## How to fix

Put statements in a handler (`on press { … }`), a fn, a task or an effect; a derived value is a `let`.

## Wrong

```asuka
component Counter {
  state n = 0
  Column {
    n += 1
    Text("{n}")
  }
}
```

## Right

```asuka
component Counter {
  state n = 0
  Column {
    Button("Add") { on press { n += 1 } }
    Text("{n}")
  }
}
```
