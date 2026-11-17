# missing-await

A call of an async function without `await`.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An async function's result comes later; `await` says where the code waits for it, so every place another handler may run in between is written down.

## How to fix

Write `await f(…)` (`try await` if it throws), or start it on its own with `spawn`.

## Wrong

```asuka
async fn fetchCount() -> int {
  await sleep(1s)
  return 1
}

async fn total() -> int {
  return fetchCount()
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
