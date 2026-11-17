# tr-in-package

`tr` in a package that isn't the app.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

The catalogs are the app's: a library can't know what messages the apps that use it have.

## How to fix

Take the text as a parameter, and let the app pass `tr.key()`.

## Wrong

`app.asuka`

```asuka
import @acme/ui

component Home {
  ui.Header()
}
```

`asuka.toml`

```toml
name = "home"

[dependencies]
"@acme/ui" = "file:./ui"
```

`locales/en.json`

```json
{"title": "Home"}
```

`ui/asuka.toml`

```toml
name = "@acme/ui"
version = "1.0.0"
```

`ui/ui.asuka`

```asuka
pub component Header {
  Text(tr.title())
}
```

## Right

`app.asuka`

```asuka
import @acme/ui

component Home {
  ui.Header(tr.title())
}
```

`asuka.toml`

```toml
name = "home"

[dependencies]
"@acme/ui" = "file:./ui"
```

`locales/en.json`

```json
{"title": "Home"}
```

`ui/asuka.toml`

```toml
name = "@acme/ui"
version = "1.0.0"
```

`ui/ui.asuka`

```asuka
pub component Header(title: string) {
  Text(title)
}
```
