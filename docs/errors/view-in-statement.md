# view-in-statement

A view made in code that runs.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Views are declared in a component's body, where they stay as long as what decides them holds. A view made in a handler or a function would be made and dropped, shown nowhere.

## How to fix

Declare the view in the body, under an `if` on state the handler sets.

## Wrong

```asuka
component Hello {
  Button("Greet") {
    on press { Text("Hi!") }
  }
}
```

## Right

```asuka
component Hello {
  state greeted = false
  Column {
    Button("Greet") { on press { greeted = true } }
    if greeted { Text("Hi!") }
  }
}
```
