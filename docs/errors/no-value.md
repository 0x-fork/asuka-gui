# no-value

Something used as a value that gives none.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An `if` without an `else`, a block whose last line isn't an expression, a call of a function that returns nothing, `()` and `_` have no value to give.

## How to fix

Give the `if` an `else`, end the block with the value, or use a function that returns one.

## Wrong

```asuka
fn sign(n: int) -> int = if n < 0 { -1 }
```

## Right

```asuka
fn sign(n: int) -> int = if n < 0 { -1 } else { 1 }
```
