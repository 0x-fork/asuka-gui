# load-outside-component

`load` outside a component's or a store's `let`.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`load` keeps an async result for as long as what declares it is shown, running again when its arguments change: a component's or a store's `let`. A function has no such lifetime.

## How to fix

Declare `let x = load f(…)` in the component, or `await` the call in the function.

## Wrong

```asuka
async fn fetchName() -> string {
  await sleep(1ms)
  return "Ana"
}

fn name() {
  let n = load fetchName()
}
```

## Right

```asuka
async fn fetchName() -> string {
  await sleep(1ms)
  return "Ana"
}

component Name {
  let n = load fetchName()
  Text(n.value ?? "…")
}
```
