# prop-or-argument

パラメーター、プロパティ、イベント、子が置き場所を間違えている。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

それぞれ置き場所が 1 つに決まっています。ヘッダーのパラメーターは括弧の中、プロパティはブロックの中に `name: value`、イベントは `on name { }`、子はブロックのビューです。

## 直し方

それぞれの場所に移します（`Badge(3) { compact: true; on tap { … } }`）。

## 誤った例

```asuka
component Badge {
  prop count = 0
  Text("{count}")
}

component Inbox {
  Badge(count: 3)
}
```

## 正しい例

```asuka
component Badge {
  prop count = 0
  Text("{count}")
}

component Inbox {
  Badge { count: 3 }
}
```
