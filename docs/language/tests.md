# Tests

```asuka
test "archiving removes the row" {
  let app = mount(Inbox(), provide: Mailbox.fake([sample]))
  await app.idle()
  app.tap("Archive")
  app.expectNo("Hello")
}
```

`expect(cond)` fails a test when cond is false, saying what failed and
where (with a comparison's two sides). JSON comes from `std/json`:
`let notes: [Note] = try json.decode(text)` reads what's expected of it
(or `json.decode<[Note]>(text)`), `json.encode(notes)` writes it. A
generic function may read or write its `T` as JSON; each call then
needs a `T` JSON can say, which the compiler checks where it's called.

`asuka test` runs a package's tests with no device: it lays views out as
the platforms do, measures text at a fixed width per character, and time
moves only when nothing else can run (a `sleep(5s)` in a test takes no
time). `mount` returns the app, to drive and check: `tap`, `longPress`,
`type(_, into:)`, `submit`, `toggle`, `slide`, `select`, `swipe(_,
action:)`, `refresh`, `scrollTo`, `back`, `dismiss`, `pickDate`,
`pickColor`, `expect`, `expectNo`, `texts()`, `disabled(text)`,
`until`, `waitFor`, `idle`; `in(.navigationBar)`, `in(.sidebar)` and
`nextTo(label)` narrow what a text names; with a router, `mount(…, at:
"/inbox/1")`, `href`, `history()`, `goBack()`, `goForward()`. See
[Testing](../testing.md).
