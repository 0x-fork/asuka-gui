# type-arguments

型引数の数が違う。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

ジェネリックな型や関数は、受け取る型の数を決めています。`Set<T>` は 1 つ、`[K: V]` は 2 つ、struct は自身の宣言のとおりです。ジェネリックでない型は型引数を取りません。

## 直し方

宣言のとおりの数だけ型を渡します（`Set<int>`、`Page<Item>`）。

## 誤った例

```asuka
struct Box<T> {
  value: T
}

fn unbox(b: Box<int, string>) -> int = 0
```

## 正しい例

```asuka
struct Box<T> {
  value: T
}

fn unbox(b: Box<int>) -> int = b.value
```
