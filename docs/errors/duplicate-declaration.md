# duplicate-declaration

A name declared twice in one place.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A name means one thing where it's declared: a package's top level, a type's members, a function's parameters, an extension, a pattern. Two declarations of it would make every use ambiguous. A file for one platform (`tray_linux.asuka`) and a file every platform has can't both declare it either.

## How to fix

Rename one, or remove the one you don't need.

## Wrong

```asuka
fn label() -> string = "a"
fn label() -> string = "b"
```

## Right

```asuka
fn label() -> string = "a"
fn otherLabel() -> string = "b"
```
