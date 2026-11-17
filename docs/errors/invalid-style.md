# invalid-style

A style declared or applied in a form it doesn't take.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A style holds style props, `style:`, `use` and variants; `style:` is set once, to a style or a list of them (`style: [card, pink]`), and a list holds styles, an `if` or a `match` choosing them.

## How to fix

Put the styles in one list: `style: [a, b]`.

## Wrong

```asuka
style big {
  font: .title
}

style red {
  color: .red
}

component Title {
  Text("Hi") {
    style: big
    style: red
  }
}
```

## Right

```asuka
style big {
  font: .title
}

style red {
  color: .red
}

component Title {
  Text("Hi") {
    style: [big, red]
  }
}
```
