# plural-categories

A plural or select message whose categories don't match its language's.

A warning the compiler reports: the program builds, and it's worth a look.

## Why

Each language has its plural categories (English `one` and `other`, Arabic six). A category the language doesn't have is never chosen, and one it lacks falls back to `other`.

## How to fix

Write the language's categories, as the message lists them.

## Wrong

`app.asuka`

```asuka
component Count {
  Text(tr.items(count: 2))
}
```

`locales/en.json`

```json
{"items": "{count, plural, one {# item} few {# items} other {# items}}"}
```

## Right

`app.asuka`

```asuka
component Count {
  Text(tr.items(count: 2))
}
```

`locales/en.json`

```json
{"items": "{count, plural, one {# item} other {# items}}"}
```
