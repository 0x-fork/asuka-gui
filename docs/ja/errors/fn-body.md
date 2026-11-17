# fn-body

本体のない関数、または持てない本体を持つ関数。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

関数には本体がありますが、プラットフォームが実装するものは別です。native interface のメソッド、native fn、extern fn（C が実装）には本体がありません。

## 直し方

本体（`{ … }` か `= 式`）を書くか、native や extern の宣言から外します。

## 誤った例

```asuka
fn greeting() -> string
```

## 正しい例

```asuka
fn greeting() -> string = "Hello"
```
