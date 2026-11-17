# misplaced-statement

書いた内容が意味をなさない場所にある文。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`break` と `continue` はループを抜けるのでループの中に、`return` は関数を抜けるもので `defer` やコンポーネントの本体を抜けるものではなく、`emit` はコンポーネントのイベントを発生させるのでコンポーネントの中に書きます。`let … else` は関数やハンドラーの文です。

## 直し方

意味のある場所に文を移すか、ここで本当にしたいことを書きます。

## 誤った例

```asuka
fn first(xs: [int]) -> int {
  if xs.isEmpty() { break }
  return xs[0]
}
```

## 正しい例

```asuka
fn first(xs: [int]) -> int {
  if xs.isEmpty() { return 0 }
  return xs[0]
}
```
