# missing-type

型を書かなければならない宣言。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

パラメーターは型を書きます。prop は型か、型を推論できる既定値を書きます。自分自身（現在の値、最初は `none`）を読む状態は、その値から型を推論できません。

## 直し方

型を書きます（`prop size: int`、`prop size = 12`、`state n: int = …`）。

## 誤った例

```asuka
component Label {
  prop size
  Text("{size}")
}
```

## 正しい例

```asuka
component Label {
  prop size = 12
  Text("{size}")
}
```
