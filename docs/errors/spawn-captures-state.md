# spawn-captures-state

`spawn` that reads or changes state, or what isn't a value.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`spawn` runs its block on another thread, while the main thread goes on changing state; it takes values only, copied as it starts, and gives a value back.

## How to fix

Read the state into a `let` first, and pass that; assign what `spawn` returns.

## Wrong

```asuka
component Total {
  state items: [int] = []
  state sum = 0
  task {
    sum = await spawn { items.count() }
  }
  Text("{sum}")
}
```

## Right

```asuka
component Total {
  state items: [int] = []
  state sum = 0
  task {
    let xs = items
    sum = await spawn { xs.count() }
  }
  Text("{sum}")
}
```
