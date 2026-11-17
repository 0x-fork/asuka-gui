# worker-captures-state

worker 读取了主线程的状态。

编译器报告的错误：修正之前程序无法构建。

## 原因

worker 运行在自己的线程上，而状态在主线程上，也只在主线程上变化。worker 的 fn 需要什么，就以值的形式接收什么。

## 如何修正

在主线程上读取状态，把它的值传给 `pub fn`。

## 错误示例

```asuka
state limit = 10

worker Clamp {
  pub fn cap(n: int) -> int = min(n, limit)
}
```

## 正确示例

```asuka
worker Clamp {
  pub fn cap(n: int, limit: int) -> int = min(n, limit)
}
```
