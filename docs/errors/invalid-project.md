# invalid-project

The project's asuka.toml or directory can't be read as it says.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

asuka.toml says what the app is (its accent color, its catalogs, its dependencies); a value in the wrong form there can't be used.

## How to fix

Fix the value in asuka.toml as the message says.

## Wrong

`app.asuka`

```asuka
component Home {
  Text("Notes")
}

app { Home() }
```

`asuka.toml`

```toml
name = "Notes"
accent = "purple"
```

## Right

`app.asuka`

```asuka
component Home {
  Text("Notes")
}

app { Home() }
```

`asuka.toml`

```toml
name = "Notes"
accent = "#7c3aed"
```
