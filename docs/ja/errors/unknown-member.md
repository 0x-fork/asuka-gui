# unknown-member

型、列挙型、パッケージにその名前のメンバーがない。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`.` のあとの名前は、その前にあるもののフィールド、メソッド、case、定数、またはパッケージの宣言です。何を持つかは型が決め、拡張が追加するのはその拡張が見える場所だけです。

## 直し方

既存のメンバーを使う（メッセージが提案します）か、型にそのメンバーを追加します。

## 誤った例

```asuka
struct Person {
  name: string
}

fn label(p: Person) -> string = p.nmae
```

## 正しい例

```asuka
struct Person {
  name: string
}

fn label(p: Person) -> string = p.name
```
