# message-arguments

调用消息时给的参数与它的占位符不符。

编译器报告的错误：修正之前程序无法构建。

## 原因

消息的参数以它的占位符命名（`"Hello, {name}"` 接收 `name:`），每个只能给一次，类型按目录的规定。

## 如何修正

按名字传入每个占位符：`tr.greeting(name: n)`。

## 错误示例

`app.asuka`

```asuka
component Hi {
  Text(tr.greeting())
}
```

`locales/en.json`

```json
{"greeting": "Hello, {name}"}
```

## 正确示例

`app.asuka`

```asuka
component Hi {
  Text(tr.greeting(name: "Ana"))
}
```

`locales/en.json`

```json
{"greeting": "Hello, {name}"}
```
