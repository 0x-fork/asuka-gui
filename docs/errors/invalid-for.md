# invalid-for

A `for` over something it doesn't go over.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A `for` goes over an array, a set, a map, a range, a string's chars or an iterator; a stream's values come over time (`for await`); a number isn't a list (`0..<n` is its range). `key` is a `for`'s in a view.

## How to fix

Go over what the message says: `0..<n`, `m.keys()`, `for await x in stream`.

## Wrong

```asuka
fn total(n: int) -> int {
  var sum = 0
  for i in n { sum += i }
  return sum
}
```

## Right

```asuka
fn total(n: int) -> int {
  var sum = 0
  for i in 0..<n { sum += i }
  return sum
}
```
