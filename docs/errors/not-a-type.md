# not-a-type

Something used as a type that isn't one.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Where a type goes (a parameter's, a field's, after `:`), the name must be a type: a built-in one, a struct, an enum, an interface or an alias. `void?` and `T??` aren't types either.

## How to fix

Write the type you mean: `bool` for `void?`, one `?` for an optional.

## Wrong

```asuka
fn shown(flag: void?) -> bool = flag != none
```

## Right

```asuka
fn shown(flag: bool) -> bool = flag
```
