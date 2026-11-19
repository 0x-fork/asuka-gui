# special-method

言語が呼ぶメソッド（equals、hash、description、toJSON、fromJSON）が、言語の呼べないシグネチャーになっている。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

構造体と列挙型は、自分のものを宣言しない限り、フィールドによって等しいかを比べ、ハッシュし、出力し、JSON に書かれます。自分のものは `fn equals(other: T) -> bool`、`fn hash() -> int`、`fn description() -> string`、`fn toJSON() -> J`、`static fn fromJSON(json: J) throws -> T` です。言語はこれらを ==、マップとセット、補間、JSON から呼ぶので、そこで渡すものを受け取り、期待するものを返さなければならず、async、mut、private にはできません。自分のやり方でハッシュする型は、比較も自分のやり方で行います（等しい値は同じハッシュになります）。

## 直し方

メッセージが示すシグネチャーにするか、その用途でなければ別の名前にします。

## 誤った例

```asuka
struct Money {
  cents: int

  fn equals(other: int) -> bool = cents == other
}
```

## 正しい例

```asuka
struct Money {
  cents: int

  fn equals(other: Money) -> bool = cents == other.cents
}
```
