# maybe-none

把可能为 `none` 的值当作一定存在来使用。

编译器报告的错误：修正之前程序无法构建。

## 原因

可选类型（`T?`）的值是一个 `T` 或 `none`。它的成员和 map 的条目 `m[k]` 可能不存在，所以编译器要求你说明不存在时怎么办。

## 如何修正

使用 `?.`（`p?.name`），用 `??` 给出默认值，或者先解包：`if let p = p { … }`。

## 错误示例

```asuka
struct Person {
  name: string
}

fn label(p: Person?) -> string = p.name
```

## 正确示例

```asuka
struct Person {
  name: string
}

fn label(p: Person?) -> string = p?.name ?? "nobody"
```
