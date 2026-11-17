# write-in-binding

State written where the UI follows it.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A binding, a `let` and a component's body say what stays true; they run again whenever what they read changes. A write there would run on every change it causes.

## How to fix

Write state in a handler (`on press { … }`), a fn, a task or an effect. A value computed from state is a `let`.

## Wrong

```asuka
component Counter {
  state n = 0
  let shown = if n > 9 {
    n = 0
    0
  } else {
    n
  }
  Button("{shown}") { on press { n += 1 } }
}
```

## Right

```asuka
component Counter {
  state n = 0
  Button("{n}") { on press { n = if n >= 9 { 0 } else { n + 1 } } }
}
```
