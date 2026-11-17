# recursive-type

A type that holds a whole copy of itself.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A struct's fields are inside it, so a struct whose field is itself would be infinitely large, and no value of it could be made. An alias can't name itself; a C struct can't hold itself either.

## How to fix

Make the field on the way optional (`next: Node?`) or an array (`children: [Node]`).

## Wrong

```asuka
struct Node {
  value: int
  next: Node
}
```

## Right

```asuka
struct Node {
  value: int
  next: Node? = none
}
```
