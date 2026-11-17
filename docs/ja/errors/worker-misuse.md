# worker-misuse

ワーカー自身の値を外から読んでいる、または作れない・呼べない場所でワーカーを作ったり呼んだりしている。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

ワーカーの var は、そのスレッドでワーカーの fn が読みます。`pub fn` はメインスレッドから呼び出し、ワーカーもメインスレッドで、それを所有するものが作ります。

## 直し方

値を `pub fn` から返し、`await` で呼び出します。

## 誤った例

```asuka
worker Tally {
  var n = 0
  pub fn add() { n += 1 }
}

fn count(t: Tally) -> int = t.n
```

## 正しい例

```asuka
worker Tally {
  var n = 0
  pub fn add() { n += 1 }
  pub fn count() -> int = n
}

async fn count(t: Tally) -> int = await t.count()
```
