# maybe-none

A value that may be `none` used as if it were there.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An optional (`T?`) is a `T` or `none`. Its members, and a map's entry `m[k]`, may not be there, so the compiler asks what to do when they aren't.

## How to fix

Use `?.` (`p?.name`), give a default with `??`, or unwrap it first: `if let p = p { … }`.

## Wrong

```asuka
struct Person {
  name: string
}

fn label(p: Person?) -> string = p.name
```

## Right

```asuka
struct Person {
  name: string
}

fn label(p: Person?) -> string = p?.name ?? "nobody"
```
