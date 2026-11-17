# invalid-for

`for` 遍历了它不能遍历的东西。

编译器报告的错误：修正之前程序无法构建。

## 原因

`for` 可以遍历数组、set、map、范围或迭代器；stream 的值是随时间到来的（用 `for await`）；字符串的字符是 `s.chars()`。`key` 只用于视图里的 `for`。

## 如何修正

按消息所说遍历对应的东西：`s.chars()`、`m.keys()`、`for await x in stream`。

## 错误示例

```asuka
fn letters(s: string) -> int {
  var n = 0
  for c in s { n += 1 }
  return n
}
```

## 正确示例

```asuka
fn letters(s: string) -> int {
  var n = 0
  for c in s.chars() { n += 1 }
  return n
}
```
