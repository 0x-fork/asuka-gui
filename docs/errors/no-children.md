# no-children

Children given to an element that takes none, or not those.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

An element takes children when it declares `prop children: View` (or is a container); an `Alert`'s block holds its buttons and fields only.

## How to fix

Put the views in a container (`Column`, `Row`), or beside the element.

## Wrong

```asuka
component Note {
  Text("Title") {
    Text("Body")
  }
}
```

## Right

```asuka
component Note {
  Column {
    Text("Title")
    Text("Body")
  }
}
```
