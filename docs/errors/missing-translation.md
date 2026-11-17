# missing-translation

A language's catalog without some of the source's messages.

A warning the compiler reports: the program builds, and it's worth a look.

## Why

A message a language lacks is shown in the language it falls back to. That works, and is worth knowing.

## How to fix

Translate the messages it lists, in that language's catalog.

## Wrong

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/de.json`

```json
{"hello": "Hallo"}
```

`locales/en.json`

```json
{"hello": "Hello", "bye": "Bye"}
```

## Right

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/de.json`

```json
{"hello": "Hallo", "bye": "Tschüss"}
```

`locales/en.json`

```json
{"hello": "Hello", "bye": "Bye"}
```
