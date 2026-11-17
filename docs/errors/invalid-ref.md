# invalid-ref

`ref` or `as` naming what isn't one element's view.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`ref: x` and `as x` name an element's platform view, for what reads it (its state, a scroll). A component that shows several views has no one view to name.

## How to fix

Name an element inside the component, or give the component one element to show.

## Wrong

```asuka
component Pair {
  Text("a")
  Text("b")
}

component Page {
  Pair() as p
}
```

## Right

```asuka
component Pair {
  Row {
    Text("a")
    Text("b")
  }
}

component Page {
  Pair() as p
}
```
