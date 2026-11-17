# argument-label

関数に対応するパラメーターのない名前付き引数。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

引数は順番に渡し、そのあとで名前を付けて渡します。名前はパラメーターのどれかの名前で、1 回だけ、順番に渡す引数のあとに書きます。クロージャーと組み込み関数の引数には名前がありません。

## 直し方

パラメーターの名前を使う（メッセージが提案します）か、順番に渡します。

## 誤った例

```asuka
fn area(width: int, height: int) -> int = width * height

let a = area(width: 2, hieght: 3)
```

## 正しい例

```asuka
fn area(width: int, height: int) -> int = width * height

let a = area(width: 2, height: 3)
```
