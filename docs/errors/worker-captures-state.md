# worker-captures-state

A worker that reads the main thread's state.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A worker runs on a thread of its own; state and a file's vars live on the main thread and change there. Its fns take what they need as values.

## How to fix

Read the state (or the var) on the main thread and pass its value to a `pub fn`.

## Wrong

```asuka
state limit = 10

worker Clamp {
  pub fn cap(n: int) -> int = min(n, limit)
}
```

## Right

```asuka
worker Clamp {
  pub fn cap(n: int, limit: int) -> int = min(n, limit)
}
```
