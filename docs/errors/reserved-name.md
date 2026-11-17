# reserved-name

A name the language uses itself.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Some names mean something already where this one would go: `peek` reads without following, `style` is every element's style list, `pressed` and the other interaction states are every view's variants. Declaring one would hide what the language means by it.

## How to fix

Name it otherwise.

## Wrong

```asuka
fn peek() -> int = 1
```

## Right

```asuka
fn first() -> int = 1
```
