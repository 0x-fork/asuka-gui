# not-async-call

`load`, `all` or `race` given something that isn't an async call.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`load f(…)` keeps an async result, and `all` and `race` run async calls at once. A plain value has nothing to wait for.

## How to fix

Pass an async call (`load api.user(id)`), or use the value as it is.

## Wrong

```asuka
fn one() -> int = 1

async fn both() -> int {
  let (a, b) = await all(one(), one())
  return a + b
}
```

## Right

```asuka
async fn one() -> int {
  await sleep(1ms)
  return 1
}

async fn both() -> int {
  let (a, b) = await all(one(), one())
  return a + b
}
```
