# invalid-catch

A `catch` that can't be reached, or has nothing to catch.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Catches are tried in order, so one after a catch of everything is never reached, and one after code that can't throw has nothing to take. `do` needs a catch; a catch names an error type or one of its cases.

## How to fix

Put the specific catches first, the catch of everything last, and catch only what can throw.

## Wrong

```asuka
enum ParseError: Error {
  empty
}

fn parse(s: string) throws -> int {
  if s.isEmpty() { throw ParseError.empty }
  return 1
}

fn size(s: string) -> int {
  do {
    return try parse(s)
  } catch e {
    return -1
  } catch ParseError.empty {
    return 0
  }
}
```

## Right

```asuka
enum ParseError: Error {
  empty
}

fn parse(s: string) throws -> int {
  if s.isEmpty() { throw ParseError.empty }
  return 1
}

fn size(s: string) -> int {
  do {
    return try parse(s)
  } catch ParseError.empty {
    return 0
  } catch e {
    return -1
  }
}
```
