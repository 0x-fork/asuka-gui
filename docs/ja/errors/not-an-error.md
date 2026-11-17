# not-an-error

エラー型ではない値を throw / catch している。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`throw` で投げるもの、`catch e: T` で受け取るものは、エラーとして宣言した型（`struct T: Error`、`enum T: Error`）です。そうすることで、どの catch もその `message` を読め、投げられたエラーを呼び出し側が名前で扱えます。

## 直し方

型を `: Error` と宣言するか、`Error("…")` を投げます。

## 誤った例

```asuka
struct Oops {
  why: string
}

fn check(ok: bool) throws {
  if !ok { throw Oops(why: "no") }
}
```

## 正しい例

```asuka
struct Oops: Error {
  why: string
}

fn check(ok: bool) throws {
  if !ok { throw Oops(why: "no") }
}
```
