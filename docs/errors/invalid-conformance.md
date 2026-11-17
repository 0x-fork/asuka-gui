# invalid-conformance

A type declares, or a type parameter needs, what isn't an interface.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

After `:`, a type names interfaces: `Error`, `Equatable`, `Comparable`, `Hashable` or one the program declares. A native interface is the platform's to implement, not a type's.

## How to fix

Name an interface there, or drop it.

## Wrong

```asuka
struct Score: int {
  points: int
}
```

## Right

```asuka
struct Score: Equatable {
  points: int
}
```
