# missing-return

A function with a result that can reach its end without one.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A function that says `-> T` gives a `T` on every path. The compiler follows the paths, so an `if` without an `else` or a `match` arm that returns nothing is a path without a value.

## How to fix

Return a value on every path, or end the function with a `return`.

## Wrong

```asuka
fn sign(n: int) -> int {
  if n < 0 { return -1 }
}
```

## Right

```asuka
fn sign(n: int) -> int {
  if n < 0 { return -1 }
  return 1
}
```
