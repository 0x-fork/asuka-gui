# invalid-literal

A number, a color, a duration or an escape that isn't written right.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Literals have fixed forms: a color is `#rgb`, `#rgba`, `#rrggbb` or `#rrggbbaa` in hexadecimal; a duration takes `ms`, `s`, `min` or `h`; an escape is one Asuka knows (`\n`, `\t`, `\u{…}`…). A value made when the app compiles (an icon's markup, a pattern) must parse too.

## How to fix

Write the literal in one of its forms, as the message says.

## Wrong

```asuka
let brand = #12345
```

## Right

```asuka
let brand = #123456
```
