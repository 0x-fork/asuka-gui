# peek-untracked

`peek` where nothing is followed.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`peek(x)` reads `x` without following it, in what's followed: a binding, a `let`, a state's value. A handler, a task, a test and an `effect(…)`'s body follow nothing, so `peek` there says nothing.

## How to fix

Read the value itself.

## Wrong

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { print(peek(n)) } }
}
```

## Right

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { print(n) } }
}
```
