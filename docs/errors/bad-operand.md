# bad-operand

An operator on values it doesn't take.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Each operator takes certain types: arithmetic numbers of one kind (`int` with `int`, `float` with `float`), bit operators ints, `[]` arrays, maps and bytes, `==` values of one type. A struct takes an operator from a method (`fn add` is `+`).

## How to fix

Convert one side (`float(n)`), use a method (`a.concat(b)`), or give the type the operator's method.

## Wrong

```asuka
let rest = "hello" - "h"
```

## Right

```asuka
let rest = "hello".slice(1, 5)
```
