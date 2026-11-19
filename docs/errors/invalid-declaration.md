# invalid-declaration

A declaration written in a form its place doesn't take.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A type's constant is `let name = value`, which doesn't change, and a generic type's is one for all its types, so its type can't name their parameters; a component's `let` names one value, and a default can't read fields that aren't made yet.

## How to fix

Write it in the place's form: `let name = value` for a type's constant, a `static fn` for one that needs the type's parameters.

## Wrong

```asuka
struct Money {
  cents: int

  var zero = 0
}
```

## Right

```asuka
struct Money {
  cents: int

  let zero = 0
}
```
