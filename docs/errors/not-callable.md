# not-callable

A call of something that isn't a function.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Only functions, closures and methods are called. A value isn't, a field isn't, and an interface, an enum or a C type isn't made by calling its name.

## How to fix

Call a function, use the value as it is, or make one of the enum's cases (`Kind.name`).

## Wrong

```asuka
fn twice(n: int) -> int = n(2)
```

## Right

```asuka
fn twice(n: int) -> int = n * 2
```
