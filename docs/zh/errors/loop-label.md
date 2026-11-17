# loop-label

循环名没有被使用，或者不存在。

编译器报告的错误：修正之前程序无法构建。

## 原因

`for … as outer` 给循环起名，是为了在内层循环里写 `break outer` 或 `continue outer`。没人使用的名字、两个同名的循环，或者 `break` 了一个不在其中的循环，说的都不是事实。

## 如何修正

去掉这个名字，或者用上它：`continue outer`。

## 错误示例

```asuka
fn total(xs: [int]) -> int {
  var sum = 0
  for x in xs as each {
    sum += x
  }
  return sum
}
```

## 正确示例

```asuka
fn total(xs: [int]) -> int {
  var sum = 0
  for x in xs {
    sum += x
  }
  return sum
}
```
