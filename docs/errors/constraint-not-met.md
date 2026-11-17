# constraint-not-met

A type that lacks what a generic or a method needs of it.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A set's elements and a map's keys are `Hashable`, `==` takes `Equatable` values, `sorted()` `Comparable` ones, and a generic's type parameter says what it needs (`T: Comparable`). A struct has them when its fields do; a closure has none.

## How to fix

Use a type that has it (an id instead of the whole value), or give the type what's missing.

## Wrong

```asuka
struct Handler {
  run: fn()
}

let seen = Set<Handler>()
```

## Right

```asuka
struct Handler {
  id: int
}

let seen = Set<Handler>()
```
