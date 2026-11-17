# invalid-catalog

不是有效消息文件的目录。

编译器报告的错误：修正之前程序无法构建。

## 原因

目录（`locales/<语言>.json`）是由消息组成的 JSON 对象：每个键是一个名字，每个值是一条 ICU 消息，其参数与源语言一致。

## 如何修正

在消息指出的位置修正文件：每个键对应一个字符串，占位符与源语言一致。

## 错误示例

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/en.json`

```json
{"hello": 1}
```

## 正确示例

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/en.json`

```json
{"hello": "Hello"}
```
