# misplaced-declaration

A declaration where its kind doesn't go.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Each place holds certain declarations: a file holds imports first, then types, functions and state; a struct holds fields and methods; a store state, lets, fns, tasks and effects; a worker vars, lets and fns; an enum its cases before its constants and methods; an extension methods and constants.

## How to fix

Move it where its kind goes, or use the kind this place holds (a worker's `var` for `state`).

## Wrong

```asuka
worker Index {
  state count = 0
}
```

## Right

```asuka
worker Index {
  var count = 0
}
```
