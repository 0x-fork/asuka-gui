# not-c-type

A type that can't cross to C.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An extern declaration's types are C's: `Int8`…`Int64`, `UInt8`… `UInt64`, `int`, `Float32`, `float`, `bool`, `CString`, `bytes`, `Pointer`, extern structs and types. Asuka's arrays, maps, tuples and structs have no C layout.

## How to fix

Pass `bytes` or a `Pointer` and a length, or declare an `extern struct`.

## Wrong

```asuka
extern fn sum(xs: [int]) -> int
```

## Right

```asuka
extern fn sum(xs: bytes) -> int
```
