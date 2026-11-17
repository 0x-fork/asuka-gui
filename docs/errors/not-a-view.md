# not-a-view

A value among views that isn't one.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A body and an element's block show elements and components. A number or a string is shown by an element that shows text.

## How to fix

Show it in an element: `Text("{n}")`.

## Wrong

```asuka
component Answer {
  Column {
    42
  }
}
```

## Right

```asuka
component Answer {
  Column {
    Text("42")
  }
}
```
