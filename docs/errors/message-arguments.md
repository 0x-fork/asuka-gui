# message-arguments

A message called with other arguments than its placeholders.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A message's arguments are named after its placeholders (`"Hello, {name}"` takes `name:`), each given once, of the type the catalog says.

## How to fix

Pass each placeholder by its name: `tr.greeting(name: n)`.

## Wrong

`app.asuka`

```asuka
component Hi {
  Text(tr.greeting())
}
```

`locales/en.json`

```json
{"greeting": "Hello, {name}"}
```

## Right

`app.asuka`

```asuka
component Hi {
  Text(tr.greeting(name: "Ana"))
}
```

`locales/en.json`

```json
{"greeting": "Hello, {name}"}
```
