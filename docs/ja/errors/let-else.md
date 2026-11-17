# let-else

`let … else` の else がそのまま先へ進む、またはアンラップするものがない。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`let x = optional else { … }` はブロックの残りで `x` を束縛するので、`else` からそこへ進むことはできません。`return`、`throw`、`break`、`continue` で抜けます。必ず値がある値に `else` は要りません。

## 直し方

`else` を `return`（または `throw`、`break`、`continue`）で終えるか、外します。

## 誤った例

```asuka
fn shown(name: string?) -> string {
  let n = name else { print("no name") }
  return n
}
```

## 正しい例

```asuka
fn shown(name: string?) -> string {
  let n = name else { return "" }
  return n
}
```
