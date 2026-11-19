# special-method

语言会调用的方法（equals、hash、description、toJSON、fromJSON）签名不对，语言无法调用。

编译器报告的错误：修正之前程序无法构建。

## 原因

结构体和枚举默认按字段判断相等、计算哈希、打印和写成 JSON，除非它声明了自己的：`fn equals(other: T) -> bool`、`fn hash() -> int`、`fn description() -> string`、`fn toJSON() -> J` 和 `static fn fromJSON(json: J) throws -> T`。语言会在 ==、map 和 set、插值和 JSON 里调用它们，所以它们接收和返回的必须正是这些地方传入和期待的，也不能是 async、mut 或 private 的。用自己的方式计算哈希的类型，也要用自己的方式判断相等（相等的值哈希必须相同）。

## 如何修正

按消息里说的签名来写这个方法；如果它不是为此而写的，就换个名字。

## 错误示例

```asuka
struct Money {
  cents: int

  fn equals(other: int) -> bool = cents == other
}
```

## 正确示例

```asuka
struct Money {
  cents: int

  fn equals(other: Money) -> bool = cents == other.cents
}
```
