# not-supported

Something the language doesn't do yet.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A few forms parse but aren't implemented: a generic component, a screen without the element it needs, a few expressions the compiler can't lower yet. The message says what to use instead.

## How to fix

Use the alternative the message names: a `View` or a closure prop for a generic component.

## Wrong

```asuka
component Box<T>(value: T) {
  Text("box")
}
```

## Right

```asuka
component Box(value: string) {
  Text(value)
}
```
