# アニメーション

`animate { … }` はブロックをすぐに実行し、その中でフレームや描画される内容（不透明度、背景、角の半径、シェイプのトリム）に加えた変更をネイティブにアニメーションさせます。`animate(.spring) { … }` は `Animation` を受け取ります。長さ、カーブ、遅延、繰り返し回数、スプリングの減衰と初速です。`await` すると、アニメーションが完了してから戻ります。`await` しなくてもかまいません。

```asuka
on press {
  animate { expanded = !expanded }
}
on press {
  await animate(Animation(duration: 500ms, curve: .spring)) { offset = 0 }
  done = true
}
```

`animate(() => …)` と `animate(anim, () => …)` は、ブロックの代わりにクロージャを渡す同じ書き方です。`repeat: Animation.forever` は、プロパティが再び変わるまで繰り返します。`animating()` は、いま加えている変更がアニメーションするかどうかを返します。`onFrame(t => …)` は、ディスプレイがフレームを描くたびにそのフレームの時刻を渡してクロージャを呼びます。戻り値のクロージャを呼ぶか、コンポーネントがなくなると止まります：`let stop = onFrame(t => tick(t))`。`frames()` は同じフレームを `Stream<Duration>` として返し、読まれている間だけフレームを描かせます。タスクの中で `for await t in frames() { … }` と書けば、ループが終わるかタスクがなくなったときに止まります。
