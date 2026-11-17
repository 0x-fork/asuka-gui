# invalid-variant

A variant declared or used in a way it can't work.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A variant is laid over the one element a component shows, while its condition holds; it holds style props that change with it (not layout that would move the views around it), and no variants inside it.

## How to fix

Show one element (put the views in a `Row` or a `Column`), and set in a variant only what changes with it.

## Wrong

```asuka
component Chip {
  prop selected = false
  variant selected = selected
  Text("a")
  Text("b")
}
```

## Right

```asuka
component Chip {
  prop selected = false
  variant selected = selected
  Row {
    Text("a")
    Text("b")
  }
}
```
