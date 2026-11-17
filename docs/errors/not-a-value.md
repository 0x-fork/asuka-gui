# not-a-value

A type, a package or a method used where a value goes.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A type names a kind of value, a package a set of declarations, and a method needs the value it's called on: none of them is a value to pass or store.

## How to fix

Make a value of the type (`Point(x: 1, y: 2)`), use a package's member, or call the method.

## Wrong

```asuka
struct Point {
  x: int
  y: int
}

let origin = Point
```

## Right

```asuka
struct Point {
  x: int
  y: int
}

let origin = Point(x: 0, y: 0)
```
