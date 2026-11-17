# not-c-type

C に渡せない型。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

extern 宣言の型は C の型です。`Int8`…`Int64`、`UInt8`…`UInt64`、`int`、`Float32`、`float`、`bool`、`CString`、`bytes`、`Pointer`、extern struct と extern type。Asuka の配列、マップ、タプル、struct には C のメモリレイアウトがありません。

## 直し方

`bytes` か `Pointer` と長さを渡すか、`extern struct` を宣言します。

## 誤った例

```asuka
extern fn sum(xs: [int]) -> int
```

## 正しい例

```asuka
extern fn sum(xs: bytes) -> int
```
