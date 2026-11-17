# type-mismatch

A value of another type than the one expected.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Every value has one type, and what takes it (a parameter, a field, a `let` with its type written, a return, the branches of an `if`) says which. Asuka converts nothing on its own: an `int` isn't a `float`, and a number isn't a string.

## How to fix

Give a value of the type expected, or convert it: `float(n)`, `int(x)`, `"{n}"`.

## Wrong

```asuka
let count: int = "three"
```

## Right

```asuka
let count: int = 3
```
