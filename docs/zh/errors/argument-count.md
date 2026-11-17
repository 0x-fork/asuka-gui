# argument-count

调用时参数太多或太少。

编译器报告的错误：修正之前程序无法构建。

## 原因

调用要为每个没有默认值的参数给出值，而且不能多于参数的个数。case 的负载、事件的值和处理函数的参数也按同样的规则计数。

## 如何修正

按函数的声明传参；只有带默认值的参数可以省略。

## 错误示例

```asuka
fn add(a: int, b: int) -> int = a + b

let sum = add(1)
```

## 正确示例

```asuka
fn add(a: int, b: int) -> int = a + b

let sum = add(1, 2)
```
