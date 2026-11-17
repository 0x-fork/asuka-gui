# invalid-catalog

A catalog that isn't a valid file of messages.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A catalog (`locales/<language>.json`) is a JSON object of messages: each key a name, each value an ICU message whose arguments are the source language's.

## How to fix

Fix the file where the message points: a string for each key, its placeholders as the source's.

## Wrong

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/en.json`

```json
{"hello": 1}
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
