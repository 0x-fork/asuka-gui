# non-exhaustive-match

没有覆盖所有情况的 `match`。

编译器报告的错误：修正之前程序无法构建。

## 原因

`match` 要为传给它的每一个值都给出结果（或执行一个分支），所以它要覆盖枚举的每个 case、bool 的每个值，其余的用 `_` 兜底。编译器会以模式的形式指出缺少什么。这样给枚举新增 case 时，所有需要处理它的 `match` 都会被找出来。

## 如何修正

为缺少的情况加上分支，或者用 `_ => …` 处理其余情况。

## 错误示例

```asuka
enum Light {
  red
  green
}

fn wait(l: Light) -> bool = match l {
  .red => true
}
```

## 正确示例

```asuka
enum Light {
  red
  green
}

fn wait(l: Light) -> bool = match l {
  .red => true
  .green => false
}
```
