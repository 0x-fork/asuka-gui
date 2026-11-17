# recursive-definition

A value or a style defined in terms of itself.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A constant, a `let` or a state is computed from what it reads; one that reads itself, through others or directly, has no first value to start from. A style that applies itself would never end.

## How to fix

Break the cycle: compute one from values that don't read it back.

## Wrong

```asuka
let width = height * 2
let height = width / 2
```

## Right

```asuka
let width = 200
let height = width / 2
```
