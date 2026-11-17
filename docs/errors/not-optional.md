# not-optional

An optional's operator on a value that's always there.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`??`, `?.`, `!`, `if let` and `??=` are for values that may be `none`. On one that can't be, they do nothing, which usually means the type isn't what the code thinks.

## How to fix

Drop the operator, or make the value optional where it's declared (`int?`).

## Wrong

```asuka
fn shown(count: int) -> int = count ?? 0
```

## Right

```asuka
fn shown(count: int?) -> int = count ?? 0
```
