# prop-not-assignable

コンポーネントが代入できない prop に代入している。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

prop は呼び出し側のものです。コンポーネントが代入できるのは、`event <prop>Change` で変化を伝えるときだけです。そうすれば、それをバインドしている呼び出し側（`bind open: shown`）が新しい値を受け取れます。また、prop は丸ごと変わります。

## 直し方

`event openChange(open: bool)` を宣言してから代入するか、コンポーネント自身の状態を持ちます。

## 誤った例

```asuka
component Disclosure {
  prop open = false
  Button("More") { on press { open = !open } }
}
```

## 正しい例

```asuka
component Disclosure {
  prop open = false
  event openChange(open: bool)
  Button("More") { on press { open = !open } }
}
```
