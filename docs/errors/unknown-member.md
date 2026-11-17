# unknown-member

A type, an enum or a package has no member of that name.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

After a `.`, a name is a field, a method, a case or a constant of what's before it, or a declaration of a package. The type decides what it has; extensions add to it only where they're visible.

## How to fix

Use one of its members (the message suggests one), or add it to the type.

## Wrong

```asuka
struct Person {
  name: string
}

fn label(p: Person) -> string = p.nmae
```

## Right

```asuka
struct Person {
  name: string
}

fn label(p: Person) -> string = p.name
```
