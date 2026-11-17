# invalid-for

A `for` over something it doesn't go over.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A `for` goes over an array, a set, a map, a range or an iterator; a stream's values come over time (`for await`); a string's characters are `s.chars()`. `key` is a `for`'s in a view.

## How to fix

Go over what the message says: `s.chars()`, `m.keys()`, `for await x in stream`.

## Wrong

```asuka
fn letters(s: string) -> int {
  var n = 0
  for c in s { n += 1 }
  return n
}
```

## Right

```asuka
fn letters(s: string) -> int {
  var n = 0
  for c in s.chars() { n += 1 }
  return n
}
```
