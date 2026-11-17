# argument-count

A call with too many or too few arguments.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A call gives each parameter without a default its value, and no more than it has. A case's payload, an event's values and a handler's parameters are counted the same way.

## How to fix

Pass what the function declares; leave out only parameters with defaults.

## Wrong

```asuka
fn add(a: int, b: int) -> int = a + b

let sum = add(1)
```

## Right

```asuka
fn add(a: int, b: int) -> int = a + b

let sum = add(1, 2)
```
