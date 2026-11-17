# unused-catalog-entry

目录里没有被使用的描述或参数。

编译器报告的警告：程序仍能构建，但值得看一看。

## 原因

描述了不存在的消息的说明（`@key`）、消息没有用到的占位符，或者译文漏掉了源语言的参数，这些都是遗留或者错误。

## 如何修正

删掉它，或者在消息里用上这个参数。

## 错误示例

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/en.json`

```json
{"hello": "Hello", "@bye": {"description": "Leaving"}}
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
{"hello": "Hello", "@hello": {"description": "A greeting"}}
```
