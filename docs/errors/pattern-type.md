# pattern-type

A pattern that can't match the value's type.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A pattern in `match`, `if let` or `catch` describes values of the matched type: a literal of that type, one of its cases, a tuple of its size. One of another type would never match.

## How to fix

Write a pattern of the value's type: `1`, not `"one"`, for an int.

## Wrong

```asuka
fn name(n: int) -> string = match n {
  "one" => "1"
  _ => "?"
}
```

## Right

```asuka
fn name(n: int) -> string = match n {
  1 => "one"
  _ => "?"
}
```
