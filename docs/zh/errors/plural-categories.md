# plural-categories

复数或选择消息的类别与语言的类别不符。

编译器报告的警告：程序仍能构建，但值得看一看。

## 原因

每种语言都有自己的复数类别（英语是 `one` 和 `other`，阿拉伯语有六个）。语言没有的类别永远不会被选中，缺少的类别则会退回到 `other`。

## 如何修正

按消息列出的类别写出这种语言的类别。

## 错误示例

`app.asuka`

```asuka
component Count {
  Text(tr.items(count: 2))
}
```

`locales/en.json`

```json
{"items": "{count, plural, one {# item} few {# items} other {# items}}"}
```

## 正确示例

`app.asuka`

```asuka
component Count {
  Text(tr.items(count: 2))
}
```

`locales/en.json`

```json
{"items": "{count, plural, one {# item} other {# items}}"}
```
