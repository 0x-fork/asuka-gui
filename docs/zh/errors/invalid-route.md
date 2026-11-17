# invalid-route

页面的地址，或者路由无法完成的导航。

编译器报告的错误：修正之前程序无法构建。

## 原因

带路由的页面，其模式是字符串字面量，里面的 `:name` 就是它的参数，类型必须是地址能表达的（int、float、bool、string、枚举的 case）。导航到某个页面时，必须恰好有一条到它的路由。

## 如何修正

在模式里按页面的声明写出它的参数。

## 错误示例

```asuka
screen Profile(id: int) at "/users/:name" {
  Text("{id}")
}
```

## 正确示例

```asuka
screen Profile(id: int) at "/users/:id" {
  Text("{id}")
}
```
