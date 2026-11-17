# duplicate-declaration

同一个地方声明了两次的名称。

编译器报告的错误：修正之前程序无法构建。

## 原因

一个名称在声明它的地方只能表示一样东西：包的顶层、类型的成员、函数的参数、扩展、模式都是如此。声明两次会让每一处使用都产生歧义。某个平台专用的文件（`tray_linux.asuka`）和所有平台共用的文件也不能都声明它。

## 如何修正

给其中一个改名，或者删掉不需要的那个。

## 错误示例

```asuka
fn label() -> string = "a"
fn label() -> string = "b"
```

## 正确示例

```asuka
fn label() -> string = "a"
fn otherLabel() -> string = "b"
```
