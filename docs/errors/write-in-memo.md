# write-in-memo

State written while a `let` is computed.

A panic of the core as the app runs, in every build: what was running stops, and a crash report says this code.

## Why

A `let` is computed when it's read, as often as what it reads changes; a write there would change what others read in the middle of reading it. The compiler reports the writes it sees (write-in-binding); this one came through a function value.

## How to fix

Write state in a handler, a fn, a task or an effect; a `let` only reads.

## Wrong

```asuka
store Counter {
  state n = 0
  state bump: fn() -> int = () => 0
  let shown = bump()
}

test "a write in a let" {
  let c = Counter()
  c.bump = () => {
    c.n += 1
    return c.n
  }
  print(c.shown)
}
```

## Right

```asuka
store Counter {
  state n = 0
  let shown = n + 1
}

test "a let that reads" {
  let c = Counter()
  c.n += 1
  expect(c.shown == 2)
}
```
