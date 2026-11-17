# unknown-event

A handler or an `emit` of an event the component doesn't declare.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`on name { }` handles an event the element reports, and `emit name` raises one its component declares (`event name`). One that isn't there would never run.

## How to fix

Use one of its events (the message suggests one), or declare it: `event name(…)`.

## Wrong

```asuka
component Go {
  Button("Go") { on tap { print("go") } }
}
```

## Right

```asuka
component Go {
  Button("Go") { on press { print("go") } }
}
```
