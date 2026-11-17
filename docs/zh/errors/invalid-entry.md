# invalid-entry

程序的 `fn main` 声明方式与它的运行方式不符。

编译器报告的错误：修正之前程序无法构建。

## 原因

程序从 `fn main()` 开始：它没有参数（命令行参数用 `os.args()` 获取），不返回值（用 `os.exit(code)` 带着退出码结束），不是泛型；一个包要么是应用，要么是程序，不能两者都是。

## 如何修正

声明 `fn main() { … }`，并通过 std/os 读取参数。

## 错误示例

```asuka
fn main(args: [string]) {
  print(args)
}
```

## 正确示例

```asuka
import std/os

fn main() {
  print(os.args())
}
```
