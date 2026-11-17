# unknown-prop

A prop, a slot or a variant the element doesn't have.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An element takes the props it declares, and the style props when it shows a view that takes them; a style holds style props only. A misspelt prop would otherwise do nothing, silently.

## How to fix

Use one of its props (the message suggests one).

## Wrong

```asuka
component Title {
  Text("Hello") { colour: .red }
}
```

## Right

```asuka
component Title {
  Text("Hello") { color: .red }
}
```
