# invalid-flag-spec

`flag.parse` 无法把命令行读进的类型。

编译器报告的错误：修正之前程序无法构建。

## 原因

std/flag 根据 struct 的字段读取命令行：有默认值的字段是选项，没有的是参数；`bool` 是开关，所以要有默认值。

## 如何修正

给每个开关一个默认值（`verbose: bool = false`），选项使用消息里列出的类型。

## 错误示例

```asuka
import std/flag
import std/os

struct Options {
  verbose: bool
}

fn main() {
  let o = flag.parse<Options>(os.args())
  print(o.verbose)
}
```

## 正确示例

```asuka
import std/flag
import std/os

struct Options {
  verbose: bool = false
}

fn main() {
  let o = flag.parse<Options>(os.args())
  print(o.verbose)
}
```
