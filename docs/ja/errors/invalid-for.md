# invalid-for

`for` がたどれないものをたどっている。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`for` がたどるのは配列、セット、マップ、範囲、文字列の文字、イテレーターです。ストリームの値は時間とともに届き（`for await`）、数値はリストではありません（範囲は `0..<n`）。`key` はビューの中の `for` のものです。

## 直し方

メッセージのとおりのものをたどります（`0..<n`、`m.keys()`、`for await x in stream`）。

## 誤った例

```asuka
fn total(n: int) -> int {
  var sum = 0
  for i in n { sum += i }
  return sum
}
```

## 正しい例

```asuka
fn total(n: int) -> int {
  var sum = 0
  for i in 0..<n { sum += i }
  return sum
}
```
