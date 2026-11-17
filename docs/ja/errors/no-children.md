# no-children

子を取らない要素に子を渡している、または渡す子が違う。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

要素が子を取るのは、`prop children: View` を宣言しているか、コンテナーである場合です。`Alert` のブロックに置けるのは、そのボタンと入力欄だけです。

## 直し方

ビューをコンテナー（`Column`、`Row`）に入れるか、要素の隣に置きます。

## 誤った例

```asuka
component Note {
  Text("Title") {
    Text("Body")
  }
}
```

## 正しい例

```asuka
component Note {
  Column {
    Text("Title")
    Text("Body")
  }
}
```
