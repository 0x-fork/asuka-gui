# not-json

JSON 无法读写的类型。

编译器报告的错误：修正之前程序无法构建。

## 原因

JSON 里有数字、字符串、布尔值、数组、对象和 null；由它们组成的 struct、可选类型、不带负载的枚举都能对应过去。闭包、视图和 store 没有对应的 JSON。

## 如何修正

解码成只包含数据的 struct，再从它构造其余的东西。

## 错误示例

```asuka
import std/json

struct Settings {
  onChange: fn()
}

fn read(text: string) throws -> Settings = try json.decode<Settings>(text)
```

## 正确示例

```asuka
import std/json

struct Settings {
  theme: string
}

fn read(text: string) throws -> Settings = try json.decode<Settings>(text)
```
