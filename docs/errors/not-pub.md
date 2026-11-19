# not-pub

A declaration another package, another file (private), a store or a worker keeps to itself.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

What a package declares is its own unless it says `pub`; a store's and a worker's members likewise; and what's `private` (a declaration, a type's field or method) is its file's, and its package's tests'. That keeps a package free to change what others don't use, and a type's invariants its own.

## How to fix

Mark it `pub` (or drop `private`) where it's declared, or use what's public: a type whose private fields have no default is made by its static fns.

## Wrong

`app.asuka`

```asuka
import ./util

let answer = util.secret()
```

`util/util.asuka`

```asuka
fn secret() -> int = 42
```

## Right

`app.asuka`

```asuka
import ./util

let answer = util.secret()
```

`util/util.asuka`

```asuka
pub fn secret() -> int = 42
```
