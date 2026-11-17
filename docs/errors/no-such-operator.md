# no-such-operator

An operator other languages have and Asuka doesn't.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Asuka leaves out operators that say the same as others: `++` and `--` are `+= 1` and `-= 1`, the ternary `?:` is an `if` used as a value, and comparisons don't chain.

## How to fix

Write `x += 1`, `if a { b } else { c }`, or `a < b && b < c`.

## Wrong

```asuka
fn count() -> int {
  var n = 0
  n++
  return n
}
```

## Right

```asuka
fn count() -> int {
  var n = 0
  n += 1
  return n
}
```
