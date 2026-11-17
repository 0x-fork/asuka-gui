# write-in-memo

`let` の計算中に状態に書き込んでいる。

アプリの実行中にコアが起こすパニックで、どのビルドでも検査されます。実行中の処理は止まり、クラッシュレポートにこのコードが記録されます。

## 理由

`let` は読まれたときに計算され、読むものが変わるたびに計算し直されます。そこで書き込むと、ほかが読んでいる途中でその内容を変えてしまいます。コンパイラーは見える書き込みを報告します（write-in-binding）。これは関数値を通した書き込みです。

## 直し方

状態にはハンドラー、fn、タスク、エフェクトで書き込みます。`let` は読むだけにします。

## 誤った例

```asuka
store Counter {
  state n = 0
  state bump: fn() -> int = () => 0
  let shown = bump()
}

test "a write in a let" {
  let c = Counter()
  c.bump = () => {
    c.n += 1
    return c.n
  }
  print(c.shown)
}
```

## 正しい例

```asuka
store Counter {
  state n = 0
  let shown = n + 1
}

test "a let that reads" {
  let c = Counter()
  c.n += 1
  expect(c.shown == 2)
}
```
