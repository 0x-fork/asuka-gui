# not-sendable

无法传到另一个线程的值。

编译器报告的错误：修正之前程序无法构建。

## 原因

传给 worker 的、以及从 `spawn` 返回的，都会作为值复制过去：数字、字符串、数组，以及由它们组成的 struct。闭包、store、视图和原生对象属于创建它们的线程。

## 如何修正

传入另一个线程需要的数据，而不是持有这些数据的对象。

## 错误示例

```asuka
worker Runner {
  pub fn run(f: fn() -> int) -> int = f()
}
```

## 正确示例

```asuka
worker Runner {
  pub fn twice(n: int) -> int = n * 2
}
```
