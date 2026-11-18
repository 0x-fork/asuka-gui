# test-parameter

A test's parameter of a type asuka test can't generate.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A test with parameters is a property test: `asuka test` runs it with values it generates for them, shrinks a failing one to the simplest it can find, and `--fuzz` saves it as JSON, which every later run reads back. It generates ints, floats, bools, strings, bytes and durations, and the arrays, maps from strings, sets, optionals, structs and enums made of them. A view, a closure or a store has no values to generate, and a parameter takes no default: it gets each generated value.

## How to fix

Take what the test is about as data (an id, a struct of fields), and make the rest from it in the test's body.

## Wrong

```asuka
test "runs anything" (f: fn() -> int) {
  expect(f() == f())
}
```

## Right

```asuka
test "reversing twice" (xs: [int]) {
  expect(xs.reversed().reversed() == xs)
}
```
