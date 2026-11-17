# return-value

`return` 带了函数不返回的值，或者在需要值时没有带。

编译器报告的错误：修正之前程序无法构建。

## 原因

没有 `-> T` 的函数什么也不返回，有 `-> T` 的函数每个 `return` 都返回一个 `T`。

## 如何修正

去掉这个值，或者声明返回类型（`-> T`）并在每个地方都返回值。

## 错误示例

```asuka
fn log(s: string) {
  print(s)
  return s
}
```

## 正确示例

```asuka
fn log(s: string) {
  print(s)
}
```
