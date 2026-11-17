# type-arguments

The wrong number of type arguments.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A generic type or function says how many types it takes: `Set<T>` one, `[K: V]` two, a struct its own. A type that isn't generic takes none.

## How to fix

Give as many types as it declares: `Set<int>`, `Page<Item>`.

## Wrong

```asuka
struct Box<T> {
  value: T
}

fn unbox(b: Box<int, string>) -> int = 0
```

## Right

```asuka
struct Box<T> {
  value: T
}

fn unbox(b: Box<int>) -> int = b.value
```
