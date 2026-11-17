# recursive-definition

用自己来定义自己的值或样式。

编译器报告的错误：修正之前程序无法构建。

## 原因

常量、`let` 或状态是从它读取的东西计算出来的；直接或间接读取自己的值没有可以起步的初始值。应用自身的样式则永远不会结束。

## 如何修正

打破循环：让其中一个从不会反过来读取它的值计算出来。

## 错误示例

```asuka
let width = height * 2
let height = width / 2
```

## 正确示例

```asuka
let width = 200
let height = width / 2
```
