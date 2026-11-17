# missing-try

调用会抛错的函数时没有写 `try`。

编译器报告的错误：修正之前程序无法构建。

## 原因

可能失败的函数要写 `throws`，每次调用都要写 `try`，这样每个错误可能离开的地方都写得清清楚楚。

## 如何修正

写成 `try f(…)`；出错时想得到 `none` 就用 `try? f(…)`，想给出替代值就用 `try f(…) catch { fallback }`。

## 错误示例

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) throws -> int = parse(s)
```

## 正确示例

```asuka
fn parse(s: string) throws -> int {
  if s.isEmpty() { throw Error("empty") }
  return s.count()
}

fn size(s: string) throws -> int = try parse(s)
```
