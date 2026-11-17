# not-an-error

抛出或捕获的值不是错误类型。

编译器报告的错误：修正之前程序无法构建。

## 原因

`throw` 抛出的、`catch e: T` 捕获的，必须是声明为错误的类型（`struct T: Error`、`enum T: Error`）。这样每个 catch 都能读到它的 `message`，每次抛出的错误调用方也都能指名处理。

## 如何修正

把类型声明为 `: Error`，或者抛出 `Error("…")`。

## 错误示例

```asuka
struct Oops {
  why: string
}

fn check(ok: bool) throws {
  if !ok { throw Oops(why: "no") }
}
```

## 正确示例

```asuka
struct Oops: Error {
  why: string
}

fn check(ok: bool) throws {
  if !ok { throw Oops(why: "no") }
}
```
