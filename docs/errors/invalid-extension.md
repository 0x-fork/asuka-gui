# invalid-extension

An `extend` of what can't be extended, or in a form it doesn't take.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An extension adds to a named type, built in or declared: not to a function type, a tuple or an interface. A generic extension's type parameters are the type's, and a constant or a static fn needs a type with a name to be called on.

## How to fix

Extend a named type, or declare a function instead.

## Wrong

```asuka
extend fn() -> int {
  fn twice() -> int = self() * 2
}
```

## Right

```asuka
fn twice(f: fn() -> int) -> int = f() * 2
```
