# unused-catalog-entry

A catalog's description or argument that nothing uses.

A warning the compiler reports: the program builds, and it's worth a look.

## Why

A description (`@key`) of a message that isn't there, a placeholder a message doesn't use, or a translation that leaves out the source's argument is a leftover or a mistake.

## How to fix

Remove it, or use the argument in the message.

## Wrong

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/en.json`

```json
{"hello": "Hello", "@bye": {"description": "Leaving"}}
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
{"hello": "Hello", "@hello": {"description": "A greeting"}}
```
