# async-in-effect

An `await` in an effect.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An effect runs at once, and again whenever what it read changes; a run that waited would still be running when the next one starts. Async work belongs to a task, which restarts by cancelling the run before.

## How to fix

Use `task(what) { … }`: it runs when the component appears and again, cancelling the last run, when `what` changes.

## Wrong

```asuka
async fn save(n: int) {
  await sleep(1s)
}

component Counter {
  state n = 0
  effect { await save(n) }
  Button("{n}") { on press { n += 1 } }
}
```

## Right

```asuka
async fn save(n: int) {
  await sleep(1s)
}

component Counter {
  state n = 0
  task(n) { await save(n) }
  Button("{n}") { on press { n += 1 } }
}
```
