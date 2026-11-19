# inline-snapshot

呼び出しに直接書かれた文字列ではないインラインスナップショット。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`expectInlineSnapshot(value, "…")` は値を出力した結果を呼び出しの中の文字列と比べ、値が変わると `asuka test --update` がソースのその文字列を書き換えます。プログラムが計算する文字列や補間のある文字列は、書き換えられるテキストではありません。

## 直し方

スナップショットを文字列リテラルとして書くか、省略して（`expectInlineSnapshot(value)`）最初の実行で `asuka test` に書かせます。

## 誤った例

```asuka
fn total() -> int = 42

test "total" {
  let want = "42"
  expectInlineSnapshot(total(), want)
}
```

## 正しい例

```asuka
fn total() -> int = 42

test "total" {
  expectInlineSnapshot(total(), "42")
}
```
