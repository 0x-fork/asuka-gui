# local-fn

代码块里的函数用了只有文件顶层函数才有的特性。

编译器报告的错误：修正之前程序无法构建。

## 原因

在代码块里声明的函数是这个代码块的辅助函数：它不能是 `pub`、泛型、native 或 extern，参数也只能按顺序传入，不能有默认值。

## 如何修正

需要这些特性时，把它声明在文件顶层。

## 错误示例

```asuka
fn pair() -> int {
  fn both<T>(x: T) -> [T] = [x, x]
  return both(1).count()
}
```

## 正确示例

```asuka
fn both<T>(x: T) -> [T] = [x, x]

fn pair() -> int {
  return both(1).count()
}
```
