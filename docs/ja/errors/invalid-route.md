# invalid-route

画面のアドレス、またはルートでは作れないナビゲーション。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

ルーティングされる画面のパターンは文字列リテラルで、その `:name` がパラメーターです。型はアドレスで表せるもの（int、float、bool、string、列挙型の case）です。画面へのナビゲーションには、そこへのルートがちょうど 1 つ必要です。

## 直し方

画面のパラメーターを、宣言のとおりにパターンに書きます。

## 誤った例

```asuka
screen Profile(id: int) at "/users/:name" {
  Text("{id}")
}
```

## 正しい例

```asuka
screen Profile(id: int) at "/users/:id" {
  Text("{id}")
}
```
