# not-an-error

A value thrown or caught that isn't an error type.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

What `throw` throws and `catch e: T` catches is a type declared an error (`struct T: Error`, `enum T: Error`), so every catch can read its `message` and every throw is one a caller can name.

## How to fix

Declare the type `: Error`, or throw `Error("…")`.

## Wrong

```asuka
struct Oops {
  why: string
}

fn check(ok: bool) throws {
  if !ok { throw Oops(why: "no") }
}
```

## Right

```asuka
struct Oops: Error {
  why: string
}

fn check(ok: bool) throws {
  if !ok { throw Oops(why: "no") }
}
```
