# invalid-extern

An extern declaration C can't stand behind.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An extern fn is a C function's name; an extern type's `release` is an extern fn of the package that takes one of it; an extern struct is C's layout, with nothing to declare.

## How to fix

Declare the release function `extern fn close(x: T)`, and the rest as C has it.

## Wrong

```asuka
extern type Handle with release close

fn close(h: Handle) {}
```

## Right

```asuka
extern type Handle with release close

extern fn close(h: Handle)
```
