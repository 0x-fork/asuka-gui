# effect-loop

Effects that keep dirtying each other in one flush.

A panic of the core as the app runs, in every build: what was running stops, and a crash report says this code.

## Why

An effect that writes what it, or another effect, reads runs again. When the writes never settle, the flush would never end: after 100 passes the core stops it, naming the effect, its line, and the state it wrote last. The compiler reports the loops it can see (effect-writes-what-it-follows); this is one it can't, a write through a function value.

## How to fix

Give the value one source: compute it with a `let` or a state that follows, or write it in the handler that changes what the effect reads.

## Wrong

```asuka
store Looper {
  state runs = 0
  state step: fn() = () => {}

  effect {
    let _ = runs
    step()
  }
}

test "a loop" {
  let l = Looper()
  l.step = () => { l.runs += 1 }
}
```

## Right

```asuka
store Looper {
  state runs = 0
  state step: fn() = () => {}

  effect(step) {
    step()
  }
}

test "no loop" {
  let l = Looper()
  l.step = () => { l.runs += 1 }
  expect(l.runs == 1)
}
```
