# local-fn

A function in a block declared with what only a file's functions have.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A function declared in a block is a helper of that block: it isn't `pub`, generic, native or extern, and it takes its arguments in order, without defaults.

## How to fix

Declare it at the top of the file for any of those.

## Wrong

```asuka
fn pair() -> int {
  fn both<T>(x: T) -> [T] = [x, x]
  return both(1).count()
}
```

## Right

```asuka
fn both<T>(x: T) -> [T] = [x, x]

fn pair() -> int {
  return both(1).count()
}
```
