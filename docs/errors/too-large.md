# too-large

A function too large for the bytecode.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An instruction names at most 65,535 values, and a function holds at most 65,534 at once. A literal or a function past that can't be encoded.

## How to fix

Split the function, or keep the data in a file (`embed.json`) instead of a literal.

## Wrong

```asuka
fn sum() -> int {
  let v0 = 0
  let v1 = 1
  let v2 = 2
  let v3 = 3
  let v4 = 4
  …
  let v69999 = 69999
  return v0 + v69999
}

let total = sum()
```

## Right

```asuka
fn sum() -> int {
  let v0 = 0
  let v1 = 1
  let v2 = 2
  return v0 + v2
}

let total = sum()
```
