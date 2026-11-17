# invalid-catch

执行不到或无错可捕的 `catch`。

编译器报告的错误：修正之前程序无法构建。

## 原因

catch 会按顺序尝试，所以写在“捕获一切”的 catch 后面的 catch 永远执行不到；写在不会抛错的代码后面的 catch 也没有东西可捕。`do` 至少要有一个 catch，catch 要写错误类型或它的某个 case。

## 如何修正

把具体的 catch 放在前面，捕获一切的放在最后，并且只捕获可能抛出的错误。

## 错误示例

```asuka
enum ParseError: Error {
  empty
}

fn parse(s: string) throws -> int {
  if s.isEmpty() { throw ParseError.empty }
  return 1
}

fn size(s: string) -> int {
  do {
    return try parse(s)
  } catch e {
    return -1
  } catch ParseError.empty {
    return 0
  }
}
```

## 正确示例

```asuka
enum ParseError: Error {
  empty
}

fn parse(s: string) throws -> int {
  if s.isEmpty() { throw ParseError.empty }
  return 1
}

fn size(s: string) -> int {
  do {
    return try parse(s)
  } catch ParseError.empty {
    return 0
  } catch e {
    return -1
  }
}
```
