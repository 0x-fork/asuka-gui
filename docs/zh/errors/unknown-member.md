# unknown-member

类型、枚举或包里没有这个名称的成员。

编译器报告的错误：修正之前程序无法构建。

## 原因

`.` 后面的名称必须是前面那个东西的字段、方法、case 或常量，或者包里的声明。类型拥有哪些成员由它自己决定；扩展只在可见的地方为它添加成员。

## 如何修正

使用它已有的成员（消息会给出建议），或者把这个成员加到类型上。

## 错误示例

```asuka
struct Person {
  name: string
}

fn label(p: Person) -> string = p.nmae
```

## 正确示例

```asuka
struct Person {
  name: string
}

fn label(p: Person) -> string = p.name
```
