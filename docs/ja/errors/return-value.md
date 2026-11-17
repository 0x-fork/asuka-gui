# return-value

関数が返さない値を持つ `return`、または値が必要なのに値のない `return`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`-> T` のない関数は何も返さず、ある関数はどの `return` からも `T` を返します。

## 直し方

値を外すか、結果の型（`-> T`）を宣言してすべての場所で値を返します。

## 誤った例

```asuka
fn log(s: string) {
  print(s)
  return s
}
```

## 正しい例

```asuka
fn log(s: string) {
  print(s)
}
```
