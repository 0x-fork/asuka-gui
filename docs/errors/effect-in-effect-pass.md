# effect-in-effect-pass

An effect's write made a binding run twice in one flush.

Reported by the core as the app runs, in development builds (`asuka dev`, `asuka run`, `asuka test`), once per place in the code: release builds don't check it.

## Why

Bindings run before effects in a flush. An effect that writes what a binding reads makes the binding run again in the same flush: the screen was laid out once with the value before, for nothing.

## How to fix

Compute the value with a `let`, or a state that follows what the effect read: it settles in one pass.

## Wrong

```asuka
component Label {
  state name = "ana"
  state shown = ""
  effect { shown = name.upper() }
  Column {
    Text(shown)
    Button("Rename") { on press { name = "bo" } }
  }
}

test "a pass late" {
  let app = mount(Label())
  app.tap("Rename")
  app.expect("BO")
}
```

## Right

```asuka
component Label {
  state name = "ana"
  let shown = name.upper()
  Column {
    Text(shown)
    Button("Rename") { on press { name = "bo" } }
  }
}

test "in one pass" {
  let app = mount(Label())
  app.tap("Rename")
  app.expect("BO")
}
```
