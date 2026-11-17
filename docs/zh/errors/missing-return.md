# missing-return

有返回值的函数可能在没有返回值的情况下运行到结尾。

编译器报告的错误：修正之前程序无法构建。

## 原因

声明了 `-> T` 的函数在每条路径上都要给出一个 `T`。编译器会跟踪每条路径，因此没有 `else` 的 `if`、或者某个不返回的 `match` 分支，都是一条没有值的路径。

## 如何修正

在每条路径上返回值，或者在函数结尾加上 `return`。

## 错误示例

```asuka
fn sign(n: int) -> int {
  if n < 0 { return -1 }
}
```

## 正确示例

```asuka
fn sign(n: int) -> int {
  if n < 0 { return -1 }
  return 1
}
```
