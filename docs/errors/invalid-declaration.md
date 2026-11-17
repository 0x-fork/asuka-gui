# invalid-declaration

A declaration written in a form its place doesn't take.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A file's values don't change (what changes is `state`), a type's constant is `let name = value`, a component's `let` names one value, and a default can't read fields that aren't made yet.

## How to fix

Write it in the place's form: `state x = …` for a value that changes, `let name = value`.

## Wrong

```asuka
var visits = 0
```

## Right

```asuka
state visits = 0
```
