# prop-or-argument

A parameter, a prop, an event or children given in the wrong place.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Each thing has one place: a header parameter in the parentheses, a prop as `name: value` in the block, an event as `on name { }`, children as the block's views.

## How to fix

Move it to its place: `Badge(3) { compact: true; on tap { … } }`.

## Wrong

```asuka
component Badge {
  prop count = 0
  Text("{count}")
}

component Inbox {
  Badge(count: 3)
}
```

## Right

```asuka
component Badge {
  prop count = 0
  Text("{count}")
}

component Inbox {
  Badge { count: 3 }
}
```
