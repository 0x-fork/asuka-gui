# fn-body

A function without its body, or with one it can't have.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A function has a body, unless the platform implements it: a native interface's methods, a native fn and an extern fn (C's) have none.

## How to fix

Write the body (`{ … }` or `= expression`), or drop it from a native or extern declaration.

## Wrong

```asuka
fn greeting() -> string
```

## Right

```asuka
fn greeting() -> string = "Hello"
```
