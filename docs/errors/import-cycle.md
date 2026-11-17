# import-cycle

Packages that import each other.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A package is checked after what it imports, so two that import each other, directly or through others, have no order to be checked in.

## How to fix

Move what both need into a third package they both import.

## Wrong

`app.asuka`

```asuka
import ./a

let n = a.one()
```

`a/a.asuka`

```asuka
import ../b

pub fn one() -> int = 1
pub fn two() -> int = b.two()
```

`b/b.asuka`

```asuka
import ../a

pub fn two() -> int = a.one() + 1
```

## Right

`app.asuka`

```asuka
import ./a

let n = a.one()
```

`a/a.asuka`

```asuka
import ../b

pub fn one() -> int = 1
pub fn two() -> int = b.two()
```

`b/b.asuka`

```asuka
pub fn two() -> int = 2
```
