# effect-in-effect-pass

エフェクトの書き込みで、1 回のフラッシュの中でバインディングが 2 回実行された。

アプリの実行中にコアが報告します。検査するのは開発ビルド（`asuka dev`、`asuka run`、`asuka test`）だけで、コードの場所ごとに 1 回だけ報告されます。リリースビルドでは検査しません。

## 理由

フラッシュでは、バインディングがエフェクトより先に実行されます。バインディングが読むものにエフェクトが書き込むと、同じフラッシュの中でバインディングがもう一度実行されます。画面は前の値で一度レイアウトされたことになり、それは無駄です。

## 直し方

値を `let` か、エフェクトが読むものに追従する状態で計算します。そうすれば 1 パスで落ち着きます。

## 誤った例

```asuka
component Label {
  state name = "ana"
  state shown = ""
  effect { shown = name.upper() }
  Column {
    Text(shown)
    Button("Rename") { on press { name = "bo" } }
  }
}

test "a pass late" {
  let app = mount(Label())
  app.tap("Rename")
  app.expect("BO")
}
```

## 正しい例

```asuka
component Label {
  state name = "ana"
  let shown = name.upper()
  Column {
    Text(shown)
    Button("Rename") { on press { name = "bo" } }
  }
}

test "in one pass" {
  let app = mount(Label())
  app.tap("Rename")
  app.expect("BO")
}
```
