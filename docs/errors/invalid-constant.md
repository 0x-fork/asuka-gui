# invalid-constant

A value made when the app compiles that isn't a literal, or doesn't parse.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Some values are made when the app compiles, from a string literal: an SVG icon from its markup, a shader's program from its source. Text made at run time can't be, and markup that doesn't parse has nothing to make.

## How to fix

Pass the literal itself, and fix what the message says doesn't parse.

## Wrong

```asuka
import std/svg

let icon = svg.Icon("<svg")
```

## Right

```asuka
import std/svg

let icon = svg.Icon(r"""<svg viewBox="0 0 24 24"><path d="M12 3v12"/></svg>""")
```
