# tr-in-package

在不是应用的包里使用了 `tr`。

编译器报告的错误：修正之前程序无法构建。

## 原因

目录属于应用：库无法知道使用它的应用里有哪些消息。

## 如何修正

把文本作为参数接收，由应用传入 `tr.key()`。

## 错误示例

`app.asuka`

```asuka
import @acme/ui

component Home {
  ui.Header()
}
```

`asuka.toml`

```toml
name = "home"

[dependencies]
"@acme/ui" = "file:./ui"
```

`locales/en.json`

```json
{"title": "Home"}
```

`ui/asuka.toml`

```toml
name = "@acme/ui"
version = "1.0.0"
```

`ui/ui.asuka`

```asuka
pub component Header {
  Text(tr.title())
}
```

## 正确示例

`app.asuka`

```asuka
import @acme/ui

component Home {
  ui.Header(tr.title())
}
```

`asuka.toml`

```toml
name = "home"

[dependencies]
"@acme/ui" = "file:./ui"
```

`locales/en.json`

```json
{"title": "Home"}
```

`ui/asuka.toml`

```toml
name = "@acme/ui"
version = "1.0.0"
```

`ui/ui.asuka`

```asuka
pub component Header(title: string) {
  Text(title)
}
```
