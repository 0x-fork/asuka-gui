# undefined-name

A name nothing declares where it's used.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A name means what a declaration in scope says: a local, a parameter, a member of the type it's in, a declaration of the package, or one an import brings. A misspelling, a name of another package that isn't imported, or `self` where there is no value is none of these.

## How to fix

Fix the spelling (the message suggests a name close to it), import the package that declares it, or declare it.

## Wrong

```asuka
fn double(count: int) -> int = cuont * 2
```

## Right

```asuka
fn double(count: int) -> int = count * 2
```
