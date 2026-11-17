# closure-keeps-itself

A closure that calls itself through the variable it's stored in.

A warning the compiler reports: the program builds, and it's worth a look.

## Why

The closure holds the variable, and the variable holds the closure: a cycle of references, which is never freed.

## How to fix

Declare a function in the block instead, `fn name(…) { … }`: it can call itself.

## Wrong

```asuka
fn countdown(n: int) -> int {
  var step: fn(int) -> int = x => x
  step = x => if x <= 0 { 0 } else { step(x - 1) }
  return step(n)
}
```

## Right

```asuka
fn countdown(n: int) -> int {
  fn step(x: int) -> int = if x <= 0 { 0 } else { step(x - 1) }
  return step(n)
}
```
