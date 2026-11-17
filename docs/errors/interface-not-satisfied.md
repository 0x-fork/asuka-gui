# interface-not-satisfied

A type that declares an interface it doesn't implement.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`struct T: Shape` promises every method `Shape` lists, with its signature, so a `Shape` can be called through without knowing the type.

## How to fix

Add the missing methods (the message lists them), with the interface's signatures.

## Wrong

```asuka
interface Shape {
  fn area() -> float
}

struct Square: Shape {
  side: float
}
```

## Right

```asuka
interface Shape {
  fn area() -> float
}

struct Square: Shape {
  side: float
  fn area() -> float = side * side
}
```
