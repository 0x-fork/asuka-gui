# native-type

プラットフォームのビューが受け取れない型のネイティブコンポーネントのプロパティやイベント。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

ネイティブコンポーネントのプロパティとイベントは、値として Swift、Kotlin、C、JavaScript に渡ります。クロージャーやストアは渡せません。

## 直し方

プラットフォームのコードが読めるデータを使い、起きたことはイベントで伝えます。

## 誤った例

```asuka
native component Dial {
  prop format: fn(int) -> string
}
```

## 正しい例

```asuka
native component Dial {
  prop value: int
  event change(value: int)
}
```
