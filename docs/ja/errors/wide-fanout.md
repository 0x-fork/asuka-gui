# wide-fanout

1 回の書き込みが 1,000 を超えるバインディングと let を、2 回以上実行した。

アプリの実行中にコアが報告します。検査するのは開発ビルド（`asuka dev`、`asuka run`、`asuka test`）だけで、コードの場所ごとに 1 回だけ報告されます。リリースビルドでは検査しません。

## 理由

書き込みは、その状態を読んだものを再び実行します。各行が配列全体を読むリストは、1 項目が変わるだけで全行を再実行します。1 行の変化に、毎回リスト全体の手間がかかります。

## 直し方

行が表示するものをパスで読む（`todos[i].done`、`for t in todos key t.id`）か、セットの `contains` を使い、書き込みがそれを読むものだけを実行するようにします。

## 誤った例

```asuka
component Grid {
  state picked = 0
  Column {
    for i in 0..<1200 {
      Text(if picked == i { "picked {i}" } else { "{i}" })
    }
    Button("Pick") { on press { picked += 1 } }
  }
}

test "every row runs again" {
  let app = mount(Grid())
  app.tap("Pick")
  app.tap("Pick")
  app.expect("picked 2")
}
```

## 正しい例

```asuka
component Cell(i: int, picked: bool) {
  Text(if picked { "picked {i}" } else { "{i}" })
}

component Grid {
  state picked: Set<int> = []
  Column {
    for i in 0..<1200 {
      Cell(i, picked.contains(i))
    }
    Button("Pick") { on press { picked = [picked.count() + 1] } }
  }
}

test "only the rows that change" {
  let app = mount(Grid())
  app.tap("Pick")
  app.tap("Pick")
  app.expect("picked 2")
}
```
