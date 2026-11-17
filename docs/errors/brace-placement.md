# brace-placement

A block's `{` goes at the end of the line before.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A line ends a statement where it can, so `Row` on a line of its own is already a whole call, and a `{` on the next line would start a block of its own. Asuka reads one way only, so it asks for the brace where the block begins.

## How to fix

Move the `{` to the end of the line before: `fn f() {`, `Row {`, `if x {`.

## Wrong

```asuka
fn greet() -> string
{
  return "hi"
}
```

## Right

```asuka
fn greet() -> string {
  return "hi"
}
```
