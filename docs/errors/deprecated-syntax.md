# deprecated-syntax

A form the language replaced, which `asuka fmt` rewrites.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An element applies styles with `style:`, as a style applies others; a style's name alone in a block was the old form.

## How to fix

Run `asuka fmt -w`, or write `style: name`.

## Wrong

```asuka
style card {
  padding: 16
}

component Note {
  Column {
    style card
    Text("hi")
  }
}
```

## Right

```asuka
style card {
  padding: 16
}

component Note {
  Column {
    style: card
    Text("hi")
  }
}
```
