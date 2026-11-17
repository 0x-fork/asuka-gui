# too-large

函数太大，字节码装不下。

编译器报告的错误：修正之前程序无法构建。

## 原因

一条指令最多引用 65,535 个值，一个函数同时最多持有 65,534 个值。超过这个上限的字面量或函数无法编码。

## 如何修正

拆分函数，或者把数据放进文件（`embed.json`）而不是写成字面量。

## 错误示例

```asuka
fn sum() -> int {
  let v0 = 0
  let v1 = 1
  let v2 = 2
  let v3 = 3
  let v4 = 4
  …
  let v69999 = 69999
  return v0 + v69999
}

let total = sum()
```

## 正确示例

```asuka
fn sum() -> int {
  let v0 = 0
  let v1 = 1
  let v2 = 2
  return v0 + v2
}

let total = sum()
```
