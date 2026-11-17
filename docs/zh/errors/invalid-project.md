# invalid-project

项目的 asuka.toml 或目录无法按它所写的读取。

编译器报告的错误：修正之前程序无法构建。

## 原因

asuka.toml 说明应用是什么（强调色、目录、依赖）；格式不对的值无法使用。

## 如何修正

按消息所说修正 asuka.toml 里的值。

## 错误示例

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

## 正确示例

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
