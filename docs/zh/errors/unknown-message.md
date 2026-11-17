# unknown-message

`tr.key` 用了目录里没有的消息。

编译器报告的错误：修正之前程序无法构建。

## 原因

`tr` 输出应用的消息，每条对应源语言目录（`locales/en.json`）里的一个键。不存在的键在任何语言里都没有文本。

## 如何修正

使用目录里有的键（消息会给出建议），或者把它加进目录。

## 错误示例

`app.asuka`

```asuka
component Title {
  Text(tr.helo())
}
```

`locales/en.json`

```json
{"hello": "Hello"}
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
