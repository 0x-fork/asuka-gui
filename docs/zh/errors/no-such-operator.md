# no-such-operator

其他语言有、Asuka 没有的运算符。

编译器报告的错误：修正之前程序无法构建。

## 原因

Asuka 去掉了和其他写法意思重复的运算符：`++` 和 `--` 就是 `+= 1` 和 `-= 1`，三元运算符 `?:` 就是当作值使用的 `if`，比较运算也不能连写。

## 如何修正

写成 `x += 1`、`if a { b } else { c }` 或 `a < b && b < c`。

## 错误示例

```asuka
fn count() -> int {
  var n = 0
  n++
  return n
}
```

## 正确示例

```asuka
fn count() -> int {
  var n = 0
  n += 1
  return n
}
```
