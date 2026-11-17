# unknown-package

An import names no package.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An import is the standard library's (`std/…`), the platform SDK's (`platform/…`), a directory beside the file (`./util`), or a dependency asuka.toml names. A path that's none of them, or a package that was renamed, can't be read.

## How to fix

Fix the path, add the dependency (`asuka add`), or use the package's new name.

## Wrong

```asuka
import std/clock

let started = clock.now()
```

## Right

```asuka
import std/time

let started = time.now()
```
