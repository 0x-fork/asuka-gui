# loop-label

A loop's name that isn't used, or isn't there.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`for … as outer` names a loop for a `break outer` or `continue outer` inside another one. A name nothing uses, two loops of one name, or a `break` of a loop it isn't in says something that isn't so.

## How to fix

Drop the name, or use it: `continue outer`.

## Wrong

```asuka
fn total(xs: [int]) -> int {
  var sum = 0
  for x in xs as each {
    sum += x
  }
  return sum
}
```

## Right

```asuka
fn total(xs: [int]) -> int {
  var sum = 0
  for x in xs {
    sum += x
  }
  return sum
}
```
