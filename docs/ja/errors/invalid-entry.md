# invalid-entry

実行のされ方と違う形で宣言されたプログラムの `fn main`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

プログラムは `fn main()` から始まります。パラメーターはなく（引数は `os.args()`）、何も返さず（終了コードは `os.exit(code)`）、ジェネリックでもありません。パッケージはアプリかプログラムのどちらかで、両方にはなれません。

## 直し方

`fn main() { … }` と宣言し、引数は std/os で読みます。

## 誤った例

```asuka
fn main(args: [string]) {
  print(args)
}
```

## 正しい例

```asuka
import std/os

fn main() {
  print(os.args())
}
```
