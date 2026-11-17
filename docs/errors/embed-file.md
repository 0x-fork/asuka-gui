# embed-file

`embed` of a file the package doesn't have.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`embed.text(path)` and its kin read a file into the app as it compiles: the path is a string literal, relative to the package's directory, inside it.

## How to fix

Fix the path, or add the file to the package.

## Wrong

```asuka
import std/embed

let terms = embed.text("terms.md")
```

## Right

`app.asuka`

```asuka
import std/embed

let terms = embed.text("terms.md")
```

`terms.md`

```markdown
# Terms
```
