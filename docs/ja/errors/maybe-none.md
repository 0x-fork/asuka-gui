# maybe-none

`none` かもしれない値を、あるものとして使っている。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

オプショナル（`T?`）は `T` か `none` です。そのメンバーやマップの要素 `m[k]` は存在しないことがあるので、ないときにどうするかをコンパイラーが尋ねます。

## 直し方

`?.` を使う（`p?.name`）か、`??` で既定値を与えるか、先にアンラップします（`if let p = p { … }`）。

## 誤った例

```asuka
struct Person {
  name: string
}

fn label(p: Person?) -> string = p.name
```

## 正しい例

```asuka
struct Person {
  name: string
}

fn label(p: Person?) -> string = p?.name ?? "nobody"
```
