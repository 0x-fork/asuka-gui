# unknown-event

コンポーネントが宣言していないイベントのハンドラーや `emit`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`on name { }` は要素が伝えるイベントを処理し、`emit name` は自分のコンポーネントが宣言したイベント（`event name`）を発生させます。存在しないイベントは決して起きません。

## 直し方

既存のイベントを使う（メッセージが提案します）か、宣言します（`event name(…)`）。

## 誤った例

```asuka
component Go {
  Button("Go") { on tap { print("go") } }
}
```

## 正しい例

```asuka
component Go {
  Button("Go") { on press { print("go") } }
}
```
