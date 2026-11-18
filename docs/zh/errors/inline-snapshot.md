# inline-snapshot

内联快照不是直接写在调用里的字符串。

编译器报告的错误：修正之前程序无法构建。

## 原因

`expectInlineSnapshot(value, "…")` 把值打印出来的结果和调用里的字符串比较；值变了的时候，`asuka test --update` 会改写源代码里的这个字符串。程序算出来的字符串、带插值的字符串，都不是 update 能改写的文本。

## 如何修正

把快照写成字符串字面量，或者干脆省略（`expectInlineSnapshot(value)`），让 `asuka test` 在第一次运行时写上。

## 错误示例

```asuka
fn total() -> int = 42

test "total" {
  let want = "42"
  expectInlineSnapshot(total(), want)
}
```

## 正确示例

```asuka
fn total() -> int = 42

test "total" {
  expectInlineSnapshot(total(), "42")
}
```
