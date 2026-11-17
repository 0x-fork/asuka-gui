# missing-prop

An element without a prop it needs.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A prop without a default has no value until its caller gives one.

## How to fix

Set it in the element's block: `Badge { count: 3 }`, or give the prop a default.

## Wrong

```asuka
component Badge {
  prop count: int
  Text("{count}")
}

component Inbox {
  Badge()
}
```

## Right

```asuka
component Badge {
  prop count: int
  Text("{count}")
}

component Inbox {
  Badge { count: 3 }
}
```
