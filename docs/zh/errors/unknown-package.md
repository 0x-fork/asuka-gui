# unknown-package

导入的路径不对应任何包。

编译器报告的错误：修正之前程序无法构建。

## 原因

导入的可以是标准库（`std/…`）、平台 SDK（`platform/…`）、文件旁边的目录（`./util`），或者 asuka.toml 里列出的依赖。不属于这些的路径，或者已经改名的包，都无法读取。

## 如何修正

修正路径，添加依赖（`asuka add`），或者改用包的新名称。

## 错误示例

```asuka
import std/clock

let started = clock.now()
```

## 正确示例

```asuka
import std/time

let started = time.now()
```
