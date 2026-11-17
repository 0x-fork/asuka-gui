# unexpected-block

A block after a call that takes none.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A block after a call is its last argument, a function, or an element's props and children. A function whose last parameter isn't a function, a struct, a store or a worker takes its values in parentheses only.

## How to fix

Pass the values in the parentheses, and drop the block.

## Wrong

```asuka
fn twice(n: int) -> int = n * 2

let four = twice(2) {
  3
}
```

## Right

```asuka
fn twice(n: int) -> int = n * 2

let four = twice(2)
```
