# not-sendable

A value that can't cross to another thread.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

What goes to a worker or comes back from `spawn` is copied as a value: numbers, strings, arrays, structs of them. A closure, a store, a view or a native object belongs to the thread that made it.

## How to fix

Pass the data the other thread needs, not the object that has it.

## Wrong

```asuka
worker Runner {
  pub fn run(f: fn() -> int) -> int = f()
}
```

## Right

```asuka
worker Runner {
  pub fn twice(n: int) -> int = n * 2
}
```
