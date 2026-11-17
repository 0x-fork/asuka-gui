# unterminated

A string, a comment or an escape isn't closed.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A string ends with its `"` on the same line, a block comment with `*/`, and `\u{…}` with its brace. Without the end, the rest of the file would be read as part of it.

## How to fix

Close it. A string that spans lines is written `"""…"""`.

## Wrong

```asuka
let greeting = "Hello
```

## Right

```asuka
let greeting = "Hello"
```
