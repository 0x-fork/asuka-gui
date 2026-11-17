# constraint-not-met

ジェネリックやメソッドが必要とするものを型が持っていない。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

セットの要素とマップのキーは `Hashable`、`==` には `Equatable`、`sorted()` には `Comparable` の値が必要で、ジェネリックの型パラメーターも必要なものを書きます（`T: Comparable`）。struct はフィールドがすべて満たすときに満たし、クロージャーはどれも満たしません。

## 直し方

満たす型を使う（値全体ではなく id を使う）か、足りないものを型に与えます。

## 誤った例

```asuka
struct Handler {
  run: fn()
}

let seen = Set<Handler>()
```

## 正しい例

```asuka
struct Handler {
  id: int
}

let seen = Set<Handler>()
```
