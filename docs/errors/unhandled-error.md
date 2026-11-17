# unhandled-error

An error that can leave where nothing takes it.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An error leaves a function only when it says `throws`; a closure, a handler, a component's `let` or a value at the top of a file has no caller to give it to.

## How to fix

Mark the function `throws`, or catch the error: `try … catch { fallback }`, `do { } catch { }`, `try?`.

## Wrong

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) -> int = try parse(s)
```

## Right

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) -> int = try parse(s) catch { 0 }
```
