# not-c-type

无法传给 C 的类型。

编译器报告的错误：修正之前程序无法构建。

## 原因

extern 声明里的类型都是 C 的类型：`Int8`…`Int64`、`UInt8`…`UInt64`、`int`、`Float32`、`float`、`bool`、`CString`、`bytes`、`Pointer`、extern struct 和 extern type。Asuka 的数组、map、元组和 struct 没有 C 的内存布局。

## 如何修正

传 `bytes`，或者 `Pointer` 加长度，或者声明一个 `extern struct`。

## 错误示例

```asuka
extern fn sum(xs: [int]) -> int
```

## 正确示例

```asuka
extern fn sum(xs: bytes) -> int
```
