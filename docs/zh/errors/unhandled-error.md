# unhandled-error

错误可能从没有人接收它的地方离开。

编译器报告的错误：修正之前程序无法构建。

## 原因

只有声明了 `throws` 的函数才能让错误离开；闭包、处理函数、组件的 `let` 或文件顶层的值都没有调用方可以把错误交给它。

## 如何修正

把函数标记为 `throws`，或者捕获错误：`try … catch { fallback }`、`do { } catch { }`、`try?`。

## 错误示例

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) -> int = try parse(s)
```

## 正确示例

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) -> int = try parse(s) catch { 0 }
```
