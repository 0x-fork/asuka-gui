# unknown-message

`tr.key` of a message the catalogs don't have.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

`tr` says the app's messages, one per key of the source language's catalog (`locales/en.json`). A key that isn't there has no text in any language.

## How to fix

Use a key of the catalog (the message suggests one), or add it there.

## Wrong

`app.asuka`

```asuka
component Title {
  Text(tr.helo())
}
```

`locales/en.json`

```json
{"hello": "Hello"}
```

## Right

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/en.json`

```json
{"hello": "Hello"}
```
