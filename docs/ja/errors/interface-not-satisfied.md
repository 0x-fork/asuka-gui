# interface-not-satisfied

宣言した interface を実装していない型。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`struct T: Shape` は、`Shape` が挙げるすべてのメソッドを同じシグネチャーで持つという約束です。そのおかげで、具体的な型を知らなくても `Shape` を通して呼び出せます。

## 直し方

足りないメソッド（メッセージに一覧があります）を interface のシグネチャーで追加します。

## 誤った例

```asuka
interface Shape {
  fn area() -> float
}

struct Square: Shape {
  side: float
}
```

## 正しい例

```asuka
interface Shape {
  fn area() -> float
}

struct Square: Shape {
  side: float
  fn area() -> float = side * side
}
```
