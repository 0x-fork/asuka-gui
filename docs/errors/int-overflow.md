# int-overflow

An integer literal that doesn't fit in its type.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Each integer type holds a range: an `int` is 64 bits, from −9223372036854775808 to 9223372036854775807, a `u8` 0 to 255, an `i8` −128 to 127, a `u64` 0 to 18446744073709551615. A literal outside its type's range can't be one, and an unsigned type has no negatives.

## How to fix

Use a number in the range, a wider type (`u16`, `int`, `u64`), a `float`, or, for an exact integer past 64 bits, `std/bigint`'s `BigInt` (`BigInt.parse("9223372036854775808")!`).

## Wrong

```asuka
let big = 9223372036854775808
let level: u8 = 300
```

## Right

```asuka
let big: u64 = 9223372036854775808
let level: u16 = 300
```
