# not-pub

A declaration another package (or a store, a worker) keeps to itself.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

What a package declares is its own unless it says `pub`; a store's and a worker's members likewise. That keeps a package free to change what others don't use.

## How to fix

Mark it `pub` where it's declared, or use what the package makes public.

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
