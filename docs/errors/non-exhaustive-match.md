# non-exhaustive-match

A `match` that doesn't cover every case.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A `match` gives a value (or runs an arm) for every value it's given, so it covers every case of an enum, every value of a bool, and the rest with `_`. The compiler says what's missing, as patterns. Adding a case to an enum then shows every `match` that needs it.

## How to fix

Add arms for what's missing, or `_ => …` for the rest.

## Wrong

```asuka
enum Light {
  red
  green
}

fn wait(l: Light) -> bool = match l {
  .red => true
}
```

## Right

```asuka
enum Light {
  red
  green
}

fn wait(l: Light) -> bool = match l {
  .red => true
  .green => false
}
```
