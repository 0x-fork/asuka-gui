# await-outside-async

An `await` where nothing may wait.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Only async functions, handlers, tasks and tests wait. A plain function runs to its end at once, and a component's body is declarations, not code that runs.

## How to fix

Mark the function `async`, or load data with `let x = load f()` or a `task { }` in a component.

## Wrong

```asuka
async fn fetchCount() -> int {
  await sleep(1s)
  return 1
}

fn total() -> int {
  return await fetchCount()
}
```

## Right

```asuka
async fn fetchCount() -> int {
  await sleep(1s)
  return 1
}

async fn total() -> int {
  return await fetchCount()
}
```
