# worker-captures-state

メインスレッドの状態を読むワーカー。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

ワーカーは自分のスレッドで動き、状態はメインスレッドにあってそこでだけ変わります。ワーカーの fn は、必要なものを値として受け取ります。

## 直し方

メインスレッドで状態を読み、その値を `pub fn` に渡します。

## 誤った例

```asuka
state limit = 10

worker Clamp {
  pub fn cap(n: int) -> int = min(n, limit)
}
```

## 正しい例

```asuka
worker Clamp {
  pub fn cap(n: int, limit: int) -> int = min(n, limit)
}
```
