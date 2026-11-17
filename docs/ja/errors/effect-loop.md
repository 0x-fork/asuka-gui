# effect-loop

1 回のフラッシュの中で互いを汚し続けるエフェクト。

アプリの実行中にコアが起こすパニックで、どのビルドでも検査されます。実行中の処理は止まり、クラッシュレポートにこのコードが記録されます。

## 理由

自分や別のエフェクトが読むものに書き込むエフェクトは、再び実行されます。書き込みがいつまでも落ち着かないとフラッシュが終わらないので、100 パスでコアが止め、どのエフェクトか、その行、最後に書き込んだ状態を示します。コンパイラーは見える範囲のループを報告します（effect-writes-what-it-follows）。これは見えないほう、関数値を通した書き込みです。

## 直し方

値の出どころを 1 つにします。`let` や追従する状態で計算するか、エフェクトが読むものを変えるハンドラーで書き込みます。

## 誤った例

```asuka
store Looper {
  state runs = 0
  state step: fn() = () => {}

  effect {
    let _ = runs
    step()
  }
}

test "a loop" {
  let l = Looper()
  l.step = () => { l.runs += 1 }
}
```

## 正しい例

```asuka
store Looper {
  state runs = 0
  state step: fn() = () => {}

  effect(step) {
    step()
  }
}

test "no loop" {
  let l = Looper()
  l.step = () => { l.runs += 1 }
  expect(l.runs == 1)
}
```
