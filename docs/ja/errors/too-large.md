# too-large

バイトコードに収まらないほど大きい関数。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

1 つの命令が指せる値は最大 65,535 個、1 つの関数が同時に持てる値は最大 65,534 個です。それを超えるリテラルや関数はエンコードできません。

## 直し方

関数を分けるか、データをリテラルではなくファイル（`embed.json`）に置きます。

## 誤った例

```asuka
fn sum() -> int {
  let v0 = 0
  let v1 = 1
  let v2 = 2
  let v3 = 3
  let v4 = 4
  …
  let v69999 = 69999
  return v0 + v69999
}

let total = sum()
```

## 正しい例

```asuka
fn sum() -> int {
  let v0 = 0
  let v1 = 1
  let v2 = 2
  return v0 + v2
}

let total = sum()
```
