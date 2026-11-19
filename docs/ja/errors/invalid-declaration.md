# invalid-declaration

置かれた場所が受け付けない形で書かれた宣言。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

型の定数は `let name = value` で、変わりません。ジェネリックな型の定数はすべての型に共通の 1 つなので、その型に型パラメーターは使えません。コンポーネントの `let` は 1 つの値に名前を付け、既定値はまだ作られていないフィールドを読めません。

## 直し方

その場所の形で書きます。型の定数は `let name = value`、型パラメーターが要るものは `static fn` にします。

## 誤った例

```asuka
struct Money {
  cents: int

  var zero = 0
}
```

## 正しい例

```asuka
struct Money {
  cents: int

  let zero = 0
}
```
