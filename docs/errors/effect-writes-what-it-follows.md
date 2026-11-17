# effect-writes-what-it-follows

An effect that writes state it reads.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An effect runs again when what it read changes. Writing that itself, it runs again after its own write, a pass late: a handler has already seen the value it corrects, and the screen may show it for a frame.

## How to fix

Keep it in range where it's declared (`state n: int = min(n ?? 0, 3)`), follow something else (`effect(other) { … }`, `state x(other) = …`), or write it in the handler that changes what it reads.

## Wrong

```asuka
component Counter {
  state n = 0
  effect {
    if n > 3 { n = 3 }
  }
  Button("{n}") { on press { n += 1 } }
}
```

## Right

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { n = min(n + 1, 3) } }
}
```
