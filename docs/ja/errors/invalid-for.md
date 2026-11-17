# invalid-for

`for` がたどれないものをたどっている。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`for` がたどるのは配列、セット、マップ、範囲、イテレーターです。ストリームの値は時間とともに届き（`for await`）、文字列の文字は `s.chars()` です。`key` はビューの中の `for` のものです。

## 直し方

メッセージのとおりのものをたどります（`s.chars()`、`m.keys()`、`for await x in stream`）。

## 誤った例

```asuka
fn letters(s: string) -> int {
  var n = 0
  for c in s { n += 1 }
  return n
}
```

## 正しい例

```asuka
fn letters(s: string) -> int {
  var n = 0
  for c in s.chars() { n += 1 }
  return n
}
```
