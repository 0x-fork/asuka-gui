# non-exhaustive-match

すべての場合を網羅していない `match`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`match` は渡されたすべての値について結果を返す（または分岐を実行する）ので、列挙型のすべての case、bool のすべての値を網羅し、残りは `_` で受けます。足りないものはコンパイラーがパターンとして示します。そのため、列挙型に case を追加すると、それを扱う必要があるすべての `match` が見つかります。

## 直し方

足りない分岐を追加するか、残りを `_ => …` で受けます。

## 誤った例

```asuka
enum Light {
  red
  green
}

fn wait(l: Light) -> bool = match l {
  .red => true
}
```

## 正しい例

```asuka
enum Light {
  red
  green
}

fn wait(l: Light) -> bool = match l {
  .red => true
  .green => false
}
```
