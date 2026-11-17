# prop-not-assignable

A component assigns a prop it can't.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A prop is its caller's. A component assigns one only when it reports the change, with `event <prop>Change`, so that a caller that binds it (`bind open: shown`) gets the new value; and a prop changes whole.

## How to fix

Declare `event openChange(open: bool)` to assign it, or keep state of the component's own.

## Wrong

```asuka
component Disclosure {
  prop open = false
  Button("More") { on press { open = !open } }
}
```

## Right

```asuka
component Disclosure {
  prop open = false
  event openChange(open: bool)
  Button("More") { on press { open = !open } }
}
```
