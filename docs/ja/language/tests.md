# テスト

```asuka
test "archiving removes the row" {
  let app = mount(Inbox(), provide: Mailbox.fake([sample]))
  await app.idle()
  app.tap("Archive")
  app.expectNo("Hello")
}
```

`expect(cond)` は cond が false ならテストを失敗させ、何がどこで失敗したかを示します（比較なら両辺の値も示します）。JSON は `std/json` で扱います。`let notes: [Note] = try json.decode(text)` は求められている型として読み込み（`json.decode<[Note]>(text)` とも書けます）、`json.encode(notes)` は JSON に書き出します。ジェネリック関数も自分の `T` を JSON として読み書きできます。その場合、呼び出しごとに `T` は JSON で表せる型でなければならず、コンパイラーが呼び出し箇所でそれをチェックします。

`asuka test` は、パッケージのテストをデバイスなしで実行します。プラットフォームと同じようにビューをレイアウトし、テキストは 1 文字あたり固定の幅で測ります。また、時間はほかに実行できるものが何もないときにしか進みません（テストの中の `sleep(5s)` には時間がかかりません）。`mount` はアプリを返すので、それを操作して確かめます（`tap`、`longPress`、`type(_, into:)`、`submit`、`toggle`、`slide`、`select`、`swipe(_, action:)`、`refresh`、`scrollTo`、`back`、`dismiss`、`pickDate`、`pickColor`、`expect`、`expectNo`、`texts()`、`disabled(text)`、`until`、`waitFor`、`idle`）。`in(.navigationBar)`、`in(.sidebar)`、`nextTo(label)` で、テキストが指すものを絞り込めます。ルーターがあれば、`mount(…, at: "/inbox/1")`、`href`、`history()`、`goBack()`、`goForward()` も使えます。詳しくは[テスト](../testing.md)を参照してください。
