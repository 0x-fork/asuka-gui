# missing-try

A call of a function that throws without `try`.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A function that can fail says `throws`, and each call says `try`, so every place an error can leave from is written down.

## How to fix

Write `try f(…)`, `try? f(…)` for `none` on an error, or `try f(…) catch { fallback }`.

## Wrong

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) throws -> int = parse(s)
```

## Right

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) throws -> int = try parse(s)
```
