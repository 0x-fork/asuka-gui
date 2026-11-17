# pattern-type

与值的类型不可能匹配的模式。

编译器报告的错误：修正之前程序无法构建。

## 原因

`match`、`if let` 和 `catch` 里的模式描述的是被匹配类型的值：该类型的字面量、它的某个 case、长度相同的元组。其他类型的模式永远不会匹配。

## 如何修正

写出与值同类型的模式：对 int 用 `1`，而不是 `"one"`。

## 错误示例

```asuka
fn name(n: int) -> string = match n {
  "one" => "1"
  _ => "?"
}
```

## 正确示例

```asuka
fn name(n: int) -> string = match n {
  1 => "one"
  _ => "?"
}
```
