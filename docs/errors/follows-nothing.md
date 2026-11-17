# follows-nothing

`state x()` or `effect()` with nothing to follow.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

The parentheses of `state x(a, b)` and `effect(a, b)` say what to follow. Empty, they'd follow nothing, which is what a value read once (`peek`) or an effect that reads nothing already says.

## How to fix

Name what to follow, or write `state x = peek(…)`, `effect { … }`, `task { }`.

## Wrong

```asuka
component Counter {
  state n() = 0
  Text("{n}")
}
```

## Right

```asuka
component Counter {
  state n = 0
  Text("{n}")
}
```
