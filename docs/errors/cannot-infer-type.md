# cannot-infer-type

The compiler can't tell a value's type from what's written.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Types are inferred from values, but `[]`, `[:]`, `none` and `.case` say nothing of theirs, and a generic call whose arguments don't name its type parameters can't either.

## How to fix

Write the type where the value goes: `let ids: [int] = []`, `Type.case`, `f<T>(…)`.

## Wrong

```asuka
let ids = []
```

## Right

```asuka
let ids: [int] = []
```
