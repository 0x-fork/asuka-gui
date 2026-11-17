# native-type

A native component's prop or event of a type the platform's view can't take.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A native component's props and events cross to Swift, Kotlin, C and JavaScript as values. A closure or a store doesn't cross.

## How to fix

Use data the platform's code can read; report what happens with an event.

## Wrong

```asuka
native component Dial {
  prop format: fn(int) -> string
}
```

## Right

```asuka
native component Dial {
  prop value: int
  event change(value: int)
}
```
