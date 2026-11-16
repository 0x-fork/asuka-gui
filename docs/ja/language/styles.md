# スタイル

すべての要素がスタイルのプロパティを受け取ります。まずレイアウト（`direction`、`justify`、`align`、`gap`、`padding`、`margin`、`width`、`height`、`grow`、`shrink`、`absolute`、`top`、`grid` など）、次に見た目（`background`、`radius`、`border`、`opacity`、`shadow`、`font`、`color`、`lines` など）です。詳しくは[レイアウトとスタイル](../layout.md)を参照してください。

```asuka
style card {
  padding: 16; radius: 12; gap: 8
  background: .secondaryBackground
}

View { style card; background: .accent }   // 後に書いたプロパティが優先される
```

`pub style` は別のパッケージからも使えます。`style theme.card` と書き、その値は宣言したパッケージの中で評価されます。

スタイルを何も指定しない要素は、プラットフォーム純正のものと同じ見た目になります。

コンテナ（`View`、`Row`、`Column`、`SafeArea`）には**バリアント**を書けます。インタラクションの状態の名前を付けたスタイルのプロパティのまとまりで、コンテナがその状態にある間、元のスタイルの上に重ねられます。CSS の `:hover` や `:active` にあたるものです。状態は `hovered`、`pressed`、`dragging`（ドラッグされている間）、`dragOver`（受け付けるドラッグが上にある間）の 4 つで、この順に重なるため、`pressed` は `hovered` より優先されます。バリアントが変えるのは書いたプロパティだけで、その値はほかのプロパティと同じくバインディングです。

```asuka
View {
  padding: 16; radius: 12; background: .secondaryBackground
  hovered { borderColor: .accent }
  pressed { background: .separator; opacity: 0.8 }
  on press { open() }
}
```

ビューが状態を報告するのは、どこかでその状態が使われたときだけです。そのため `pressed` バリアントを持たないビューは、プラットフォーム自身の押下時の見た目をそのまま保ちます。バリアントの中でも `style` を使えます（`pressed { style raised }`）。ほかのビューは名前を通してビューの状態に追従できます（[ビューに名前を付ける](#ビューに名前を付ける)を参照）。`View as card { … }` と書き、任意のバインディングで `card.hovered`（あるいは `card.pressed`、`card.dragging`、`card.dragOver`）を読みます。子ビュー（グループ全体のホバー）でも兄弟のビューでも使えます。スタイル以外の反応にはハンドラーを使います：`on pressedChange(p)`、`on hoverChange(h)`。

## ビューに名前を付ける

ビューには、値を設定するのではなく、ビューに対して何かをしたいこともあります。動画を再生する、ページでスクリプトを実行する、テキストフィールドに文字を入れる、リストをスクロールする、といった操作です。`Ref` はビューのハンドルです。任意の要素に `ref: r` と書くとそのビューが `r` に入り、`Element as name` と書くと、そのブロックの残りの部分で使えるハンドルが宣言されます。

```asuka
component Player {
  let video = Ref()

  Video("intro.mp4") { ref: video }
  Button("Start over") {
    on press {
      video.seek(0)
      video.play()
    }
  }
}
```

Ref がビューを持つのは、ビューが表示されている間だけです。その前後は空なので、コマンドは何もせず、状態はすべて false になります。状態は追跡されるので、それを読むバインディングは状態の変化に追従します。

| 対象 | Ref のメンバー |
|---|---|
| すべてのビュー | `hovered`、`pressed`、`dragging`、`dragOver`（bool、追跡される）、`focus()`、`blur()` |
| `Scroll`、`List` | `scrollTo(x, y, animated: true)` |
| `Video` | `play()`、`pause()`、`seek(seconds)` |
| `WebView` | `try await eval(script)`（スクリプトの値を JSON で）、`run(script)`（完了を待たない）、`goBack()`、`goForward()`、`reload()`、`stopLoading()` |
| `TextField` | `insert(text)`、`format(.bold, on)`、`toggleFormat(.italic)`、`setLink(url)`（リッチなフィールドの選択範囲に対して） |

Ref はほかへ渡せます（`shareFiles(paths, from: button)`、`a11yFocus(field)`）。ファイルの `let` に置けば、複数のコンポーネントのビューで使えます。
