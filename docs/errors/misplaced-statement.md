# misplaced-statement

A statement where it can't do what it says.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`break` and `continue` leave a loop, so they're in one; `return` leaves a function, not a `defer` or a component's body; `emit` raises a component's event, so it's in a component; `let … else` is a statement of a function or a handler.

## How to fix

Move the statement where it means something, or write what you meant there.

## Wrong

```asuka
fn first(xs: [int]) -> int {
  if xs.isEmpty() { break }
  return xs[0]
}
```

## Right

```asuka
fn first(xs: [int]) -> int {
  if xs.isEmpty() { return 0 }
  return xs[0]
}
```
