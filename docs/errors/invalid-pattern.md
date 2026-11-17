# invalid-pattern

A pattern written in a form the language doesn't take.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Patterns have their forms: a case is `.name(…)` or `Type.name(…)`, a struct pattern names its fields (`Point(x: 0)`), `|` joins patterns that bind nothing, `let` binds names and doesn't test, and a `for` binds a name or a tuple of names.

## How to fix

Write the pattern in its form, as the message shows: `.case(x)`, `if let`, `(k, v)`.

## Wrong

```asuka
enum Shape {
  circle(radius: float)
  square(side: float)
}

fn size(s: Shape) -> float = match s {
  .circle(x) | .square(x) => x
}
```

## Right

```asuka
enum Shape {
  circle(radius: float)
  square(side: float)
}

fn size(s: Shape) -> float = match s {
  .circle(r) => r
  .square(side) => side
}
```
