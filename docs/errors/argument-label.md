# argument-label

A named argument the function has no parameter for.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Arguments come in order, then by name: a name is one of the parameters', given once, after the ones in order. A closure's arguments have no names, nor have most built-ins' (print's `separator:` and `terminator:` are named).

## How to fix

Use the parameter's name (the message suggests it), or pass the argument in order.

## Wrong

```asuka
fn area(width: int, height: int) -> int = width * height

let a = area(width: 2, hieght: 3)
```

## Right

```asuka
fn area(width: int, height: int) -> int = width * height

let a = area(width: 2, height: 3)
```
