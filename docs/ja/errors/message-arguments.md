# message-arguments

プレースホルダーと違う引数で呼び出されたメッセージ。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

メッセージの引数はプレースホルダーの名前で渡し（`"Hello, {name}"` なら `name:`）、それぞれ 1 回だけ、カタログが決めた型で渡します。

## 直し方

各プレースホルダーを名前で渡します（`tr.greeting(name: n)`）。

## 誤った例

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

## 正しい例

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
