# missing-prop

必要なプロパティのない要素。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

既定値のない prop は、呼び出し側が値を渡すまで値がありません。

## 直し方

要素のブロックで設定する（`Badge { count: 3 }`）か、prop に既定値を与えます。

## 誤った例

```asuka
component Badge {
  prop count: int
  Text("{count}")
}

component Inbox {
  Badge()
}
```

## 正しい例

```asuka
component Badge {
  prop count: int
  Text("{count}")
}

component Inbox {
  Badge { count: 3 }
}
```
