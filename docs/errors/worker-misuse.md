# worker-misuse

A worker's own values read from outside, or a worker made or called where it can't be.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A worker's vars are read by its fns, on its thread; its `pub fn`s are called from the main thread, and it's made there, by what owns it.

## How to fix

Return the value from a `pub fn`, and call it with `await`.

## Wrong

```asuka
worker Tally {
  var n = 0
  pub fn add() { n += 1 }
}

fn count(t: Tally) -> int = t.n
```

## Right

```asuka
worker Tally {
  var n = 0
  pub fn add() { n += 1 }
  pub fn count() -> int = n
}

async fn count(t: Tally) -> int = await t.count()
```
