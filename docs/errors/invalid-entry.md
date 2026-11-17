# invalid-entry

A program's `fn main` declared otherwise than it runs.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A program starts at `fn main()`: it takes no parameters (the arguments are `os.args()`), returns nothing (`os.exit(code)` ends with a code), isn't generic, and a package is an app or a program, not both.

## How to fix

Declare `fn main() { … }`, and read the arguments with std/os.

## Wrong

```asuka
fn main(args: [string]) {
  print(args)
}
```

## Right

```asuka
import std/os

fn main() {
  print(os.args())
}
```
