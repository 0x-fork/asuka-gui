# not-bindable

`bind` of a prop that doesn't report its changes.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`bind` passes state in and writes it when the element reports a change: a control's own value (`TextField`'s `text`, `Toggle`'s `on`), or a prop whose component declares `event <prop>Change`.

## How to fix

Bind a control's value, or pass the value (`text: s`) and handle its event.

## Wrong

```asuka
component Name {
  state s = ""
  Text(s) { bind text: s }
}
```

## Right

```asuka
component Name {
  state s = ""
  TextField { bind text: s }
}
```
