# loop-label

使われていない、または存在しないループ名。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`for … as outer` は、内側のループから `break outer` や `continue outer` を書くためにループに名前を付けます。誰も使わない名前、同じ名前の 2 つのループ、入っていないループへの `break` は、事実と違うことを言っています。

## 直し方

名前を外すか、使います（`continue outer`）。

## 誤った例

```asuka
fn total(xs: [int]) -> int {
  var sum = 0
  for x in xs as each {
    sum += x
  }
  return sum
}
```

## 正しい例

```asuka
fn total(xs: [int]) -> int {
  var sum = 0
  for x in xs {
    sum += x
  }
  return sum
}
```
