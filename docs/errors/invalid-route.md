# invalid-route

A screen's address or a navigation the routes can't make.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A routed screen's pattern is a string literal whose `:name`s are its parameters, of types an address says (ints, floats, bools, strings, enums' cases). Navigating to a screen needs exactly one route to it.

## How to fix

Name the screen's parameters in its pattern, as it declares them.

## Wrong

```asuka
screen Profile(id: int) at "/users/:name" {
  Text("{id}")
}
```

## Right

```asuka
screen Profile(id: int) at "/users/:id" {
  Text("{id}")
}
```
