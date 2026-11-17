# load-outside-component

コンポーネントやストアの `let` の外にある `load`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`load` は、宣言したものが表示されているあいだ非同期の結果を保ち、引数が変わると再び実行されます。コンポーネントかストアの `let` のためのもので、関数にはそうした寿命がありません。

## 直し方

コンポーネントで `let x = load f(…)` と宣言するか、関数の中で呼び出しを `await` します。

## 誤った例

```asuka
async fn fetchName() -> string {
  await sleep(1ms)
  return "Ana"
}

fn name() {
  let n = load fetchName()
}
```

## 正しい例

```asuka
async fn fetchName() -> string {
  await sleep(1ms)
  return "Ana"
}

component Name {
  let n = load fetchName()
  Text(n.value ?? "…")
}
```
