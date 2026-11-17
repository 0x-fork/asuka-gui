# invalid-flag-spec

A type `flag.parse` can't read a command line into.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

std/flag reads a command line from a struct's fields: one with a default is an option, one without an argument; a `bool` is a switch, so it has a default.

## How to fix

Give each switch a default (`verbose: bool = false`), and options the types the message lists.

## Wrong

```asuka
import std/flag
import std/os

struct Options {
  verbose: bool
}

fn main() {
  let o = flag.parse<Options>(os.args())
  print(o.verbose)
}
```

## Right

```asuka
import std/flag
import std/os

struct Options {
  verbose: bool = false
}

fn main() {
  let o = flag.parse<Options>(os.args())
  print(o.verbose)
}
```
