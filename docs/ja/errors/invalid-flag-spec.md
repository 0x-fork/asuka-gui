# invalid-flag-spec

`flag.parse` がコマンドラインを読み込めない型。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

std/flag は struct のフィールドからコマンドラインを読みます。既定値のあるフィールドはオプション、ないフィールドは引数で、`bool` はスイッチなので既定値が必要です。

## 直し方

スイッチには既定値を付け（`verbose: bool = false`）、オプションはメッセージが挙げる型にします。

## 誤った例

```asuka
import std/flag
import std/os

struct Options {
  verbose: bool
}

fn main() {
  let o = flag.parse<Options>(os.args())
  print(o.verbose)
}
```

## 正しい例

```asuka
import std/flag
import std/os

struct Options {
  verbose: bool = false
}

fn main() {
  let o = flag.parse<Options>(os.args())
  print(o.verbose)
}
```
