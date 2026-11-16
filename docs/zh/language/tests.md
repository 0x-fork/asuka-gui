# 测试

```asuka
test "archiving removes the row" {
  let app = mount(Inbox(), provide: Mailbox.fake([sample]))
  await app.idle()
  app.tap("Archive")
  app.expectNo("Hello")
}
```

`expect(cond)` 在 cond 为 false 时让测试失败，并指出失败的是什么、在哪里（如果是比较，还会给出两边的值）。JSON 由 `std/json` 处理：`let notes: [Note] = try json.decode(text)` 按期望的类型读取 JSON（也可以写成 `json.decode<[Note]>(text)`），`json.encode(notes)` 把值写成 JSON。泛型函数也可以把它的 `T` 当作 JSON 读写；这样每次调用时，`T` 都必须是 JSON 能表示的类型，编译器会在调用处检查这一点。

`asuka test` 不需要设备就能运行包里的测试：它会像各平台那样对视图进行布局，按每个字符固定宽度来测量文字，而且只有在没有其他东西可以运行时，时间才会前进（测试里的 `sleep(5s)` 不花任何时间）。`mount` 返回应用，供你驱动和检查：`tap`、`longPress`、`type(_, into:)`、`submit`、`toggle`、`slide`、`select`、`swipe(_, action:)`、`refresh`、`scrollTo`、`back`、`dismiss`、`pickDate`、`pickColor`、`expect`、`expectNo`、`texts()`、`disabled(text)`、`until`、`waitFor`、`idle`；`in(.navigationBar)`、`in(.sidebar)` 和 `nextTo(label)` 缩小文字所指的范围；有路由器时，还有 `mount(…, at: "/inbox/1")`、`href`、`history()`、`goBack()`、`goForward()`。详见[测试](../testing.md)。
