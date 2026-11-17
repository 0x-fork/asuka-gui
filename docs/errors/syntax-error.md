# syntax-error

The parser expected something else here.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

The parser reads a file token by token, and the token it found can't come next: a value is missing after an operator, a parenthesis or a brace isn't closed, two statements share a line without a `;`.

## How to fix

Read the message's "expected …, found …": write what it expected, or close what's open where it says. A mistake early on a line can hide the rest of the line's, so fix the first one first.

## Wrong

```asuka
fn total(a: int, b: int) -> int {
  return a +
}
```

## Right

```asuka
fn total(a: int, b: int) -> int {
  return a + b
}
```
