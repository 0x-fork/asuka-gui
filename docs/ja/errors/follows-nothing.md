# follows-nothing

追従するもののない `state x()` や `effect()`。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

`state x(a, b)` と `effect(a, b)` の括弧は、何に追従するかを示します。空なら何にも追従しませんが、それは 1 回だけ読む値（`peek`）や何も読まないエフェクトですでに表せます。

## 直し方

追従するものを書くか、`state x = peek(…)`、`effect { … }`、`task { }` と書きます。

## 誤った例

```asuka
component Counter {
  state n() = 0
  Text("{n}")
}
```

## 正しい例

```asuka
component Counter {
  state n = 0
  Text("{n}")
}
```
