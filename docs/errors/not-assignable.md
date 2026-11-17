# not-assignable

A change of something that can't change.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

What changes is a `var`, state, or a field or an element of one. A `let` is a constant (or, in a component, derived from what it reads), a parameter is the caller's, and a method changes its value only when it's a `mut fn`.

## How to fix

Make it a `var` (or `state`), change what a `let` reads, or declare the method `mut fn`.

## Wrong

```asuka
fn countdown() -> int {
  let n = 3
  n -= 1
  return n
}
```

## Right

```asuka
fn countdown() -> int {
  var n = 3
  n -= 1
  return n
}
```
