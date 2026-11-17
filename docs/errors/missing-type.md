# missing-type

A declaration whose type must be written.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Parameters say their types, and a prop says its type or a default to infer it from. A state that reads itself (its current value, `none` at first) can't be inferred from its own value.

## How to fix

Write the type: `prop size: int`, `prop size = 12`, `state n: int = …`.

## Wrong

```asuka
component Label {
  prop size
  Text("{size}")
}
```

## Right

```asuka
component Label {
  prop size = 12
  Text("{size}")
}
```
