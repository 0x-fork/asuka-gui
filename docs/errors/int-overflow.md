# int-overflow

An integer literal that doesn't fit in 64 bits.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An `int` is 64 bits, from −9223372036854775808 to 9223372036854775807. A literal outside that can't be one.

## How to fix

Use a smaller number, a `float`, or a `std/decimal` value for exact large numbers.

## Wrong

```asuka
let big = 9223372036854775808
```

## Right

```asuka
let big = 9223372036854775807
```
