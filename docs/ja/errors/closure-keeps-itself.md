# closure-keeps-itself

自分を保存した変数を通して自分を呼ぶクロージャー。

コンパイラーが報告する警告です。プログラムはビルドできますが、確認する価値があります。

## 理由

クロージャーは変数を保持し、変数はクロージャーを保持します。この参照の循環は解放されません。

## 直し方

代わりにブロックの中で関数 `fn name(…) { … }` を宣言します。関数は自分自身を呼び出せます。

## 誤った例

```asuka
fn countdown(n: int) -> int {
  var step: fn(int) -> int = x => x
  step = x => if x <= 0 { 0 } else { step(x - 1) }
  return step(n)
}
```

## 正しい例

```asuka
fn countdown(n: int) -> int {
  fn step(x: int) -> int = if x <= 0 { 0 } else { step(x - 1) }
  return step(n)
}
```
