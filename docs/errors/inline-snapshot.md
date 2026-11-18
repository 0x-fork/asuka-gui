# inline-snapshot

An inline snapshot that isn't a string written in the call.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`expectInlineSnapshot(value, "…")` compares what the value prints as with the string in the call, and `asuka test --update` rewrites that string in the source when the value changes. A string the program computes, or one with interpolations, isn't text the update can rewrite.

## How to fix

Write the snapshot as a string literal, or leave it out (`expectInlineSnapshot(value)`) and let `asuka test` write it on its first run.

## Wrong

```asuka
fn total() -> int = 42

test "total" {
  let want = "42"
  expectInlineSnapshot(total(), want)
}
```

## Right

```asuka
fn total() -> int = 42

test "total" {
  expectInlineSnapshot(total(), "42")
}
```
