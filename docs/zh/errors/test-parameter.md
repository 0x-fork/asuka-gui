# test-parameter

测试参数的类型是 asuka test 无法生成的。

编译器报告的错误：修正之前程序无法构建。

## 原因

带参数的测试是属性测试：`asuka test` 会为参数生成值来运行它，把失败的输入缩减到能找到的最简单的值；`--fuzz` 会把它存成 JSON，之后每次运行都会读回来。它能生成整数、浮点数、布尔值、字符串、bytes 和时长，以及由它们组成的数组、键为字符串的 map、集合、可选类型、struct 和枚举。视图、闭包和 store 没有可以生成的值；参数也不能有默认值，因为它要接收每个生成的值。

## 如何修正

让测试接收它所关心的数据（一个 id、一个由字段组成的 struct），其余的东西在测试体里用这些数据构造出来。

## 错误示例

```asuka
test "runs anything" (f: fn() -> int) {
  expect(f() == f())
}
```

## 正确示例

```asuka
test "reversing twice" (xs: [int]) {
  expect(xs.reversed().reversed() == xs)
}
```
