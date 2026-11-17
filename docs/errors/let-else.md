# let-else

A `let … else` whose else goes on, or has nothing to unwrap.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`let x = optional else { … }` binds `x` for the rest of the block, so its `else` can't go on to it: it leaves (`return`, `throw`, `break`, `continue`). A value that's always there needs no `else`.

## How to fix

End the `else` with `return` (or `throw`, `break`, `continue`), or drop it.

## Wrong

```asuka
fn shown(name: string?) -> string {
  let n = name else { print("no name") }
  return n
}
```

## Right

```asuka
fn shown(name: string?) -> string {
  let n = name else { return "" }
  return n
}
```
