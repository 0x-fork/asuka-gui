# invalid-for

`for` 遍历了它不能遍历的东西。

编译器报告的错误：修正之前程序无法构建。

## 原因

`for` 可以遍历数组、set、map、范围、字符串的字符或迭代器；stream 的值是随时间到来的（用 `for await`）；数字不是列表（它的范围是 `0..<n`）。`key` 只用于视图里的 `for`。

## 如何修正

按消息所说遍历对应的东西：`0..<n`、`m.keys()`、`for await x in stream`。

## 错误示例

```asuka
fn total(n: int) -> int {
  var sum = 0
  for i in n { sum += i }
  return sum
}
```

## 正确示例

```asuka
fn total(n: int) -> int {
  var sum = 0
  for i in 0..<n { sum += i }
  return sum
}
```
