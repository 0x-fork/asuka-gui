# invalid-conformance

型が宣言する、または型パラメーターが求めるものが interface ではない。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

型が `:` のあとに書くのは interface です。`Error`、`Equatable`、`Comparable`、`Hashable`、またはプログラムが宣言したものです。native interface はプラットフォームが実装するもので、型が宣言するものではありません。

## 直し方

そこに interface を書くか、外します。

## 誤った例

```asuka
struct Score: int {
  points: int
}
```

## 正しい例

```asuka
struct Score: Equatable {
  points: int
}
```
