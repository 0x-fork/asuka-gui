# not-json

A type JSON can't read or write.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

JSON holds numbers, strings, bools, arrays, objects and null; a struct of those, an optional, an enum without payloads map to them. A closure, a view or a store has no JSON.

## How to fix

Decode into a struct of data, and make the rest from it.

## Wrong

```asuka
import std/json

struct Settings {
  onChange: fn()
}

fn read(text: string) throws -> Settings = try json.decode<Settings>(text)
```

## Right

```asuka
import std/json

struct Settings {
  theme: string
}

fn read(text: string) throws -> Settings = try json.decode<Settings>(text)
```
