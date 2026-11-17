# missing-translation

某种语言的目录缺少源语言的部分消息。

编译器报告的警告：程序仍能构建，但值得看一看。

## 原因

缺少的消息会以回退语言显示。这样也能运行，但值得知道。

## 如何修正

在那种语言的目录里翻译列出的消息。

## 错误示例

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/de.json`

```json
{"hello": "Hallo"}
```

`locales/en.json`

```json
{"hello": "Hello", "bye": "Bye"}
```

## 正确示例

`app.asuka`

```asuka
component Title {
  Text(tr.hello())
}
```

`locales/de.json`

```json
{"hello": "Hallo", "bye": "Tschüss"}
```

`locales/en.json`

```json
{"hello": "Hello", "bye": "Bye"}
```
