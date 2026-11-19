# mixed-numbers

An operator on numbers of two types.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Asuka converts no number by itself: an `int` and a `float`, or a `u8` and an `int`, don't meet in `+`, `<` or `&`. Widening one quietly would hide where a value can be truncated, wrap or lose its fraction. An int literal takes the other side's type, so `level + 1` is fine.

## How to fix

Convert one side to the other's type: `int(level) + offset`, `float(n) * 0.5`, `u8(n)` (which throws when n doesn't fit; `u8(truncating: n)` keeps its low bits).

## Wrong

```asuka
fn total(level: u8, bonus: int) -> int = level + bonus
```

## Right

```asuka
fn total(level: u8, bonus: int) -> int = int(level) + bonus
```
