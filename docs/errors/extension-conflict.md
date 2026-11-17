# extension-conflict

An extension that adds a name the type already has, or another extension adds.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An extension adds names; it doesn't replace them, so a method's meaning never depends on what's imported. Two extensions that add the same name to a type would make every call of it ambiguous.

## How to fix

Name the method otherwise, or keep one of the extensions.

## Wrong

```asuka
extend string {
  fn count() -> int = 0
}
```

## Right

```asuka
extend string {
  fn wordCount() -> int = fields().count()
}
```
