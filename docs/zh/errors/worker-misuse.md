# worker-misuse

从外部读取了 worker 自己的值，或者在不能创建、调用 worker 的地方这么做了。

编译器报告的错误：修正之前程序无法构建。

## 原因

worker 的 var 由它的 fn 在它自己的线程上读取；它的 `pub fn` 在主线程上调用，worker 也在主线程上由拥有它的东西创建。

## 如何修正

用 `pub fn` 返回这个值，并用 `await` 调用它。

## 错误示例

```asuka
worker Tally {
  var n = 0
  pub fn add() { n += 1 }
}

fn count(t: Tally) -> int = t.n
```

## 正确示例

```asuka
worker Tally {
  var n = 0
  pub fn add() { n += 1 }
  pub fn count() -> int = n
}

async fn count(t: Tally) -> int = await t.count()
```
