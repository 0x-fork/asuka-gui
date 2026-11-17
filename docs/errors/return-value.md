# return-value

A `return` with a value the function doesn't give, or without one it does.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A function without `-> T` returns nothing, and one with it returns a `T` from each `return`.

## How to fix

Drop the value, or declare the result (`-> T`) and return one everywhere.

## Wrong

```asuka
fn log(s: string) {
  print(s)
  return s
}
```

## Right

```asuka
fn log(s: string) {
  print(s)
}
```
