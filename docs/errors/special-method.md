# special-method

A method the language calls (equals, hash, description, toJSON, fromJSON) with a signature it can't call.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A struct or an enum is equal, hashes, prints and is written as JSON by its fields unless it declares its own: `fn equals(other: T) -> bool`, `fn hash() -> int`, `fn description() -> string`, `fn toJSON() -> J` and `static fn fromJSON(json: J) throws -> T`. The language calls them from ==, maps and sets, interpolation and JSON, so each takes and gives what those pass and expect, and can't be async, mut or private. A type that hashes its own way compares its own way too (equal values hash alike).

## How to fix

Give the method the signature the message says, or another name if it isn't meant for that.

## Wrong

```asuka
struct Money {
  cents: int

  fn equals(other: int) -> bool = cents == other
}
```

## Right

```asuka
struct Money {
  cents: int

  fn equals(other: Money) -> bool = cents == other.cents
}
```
