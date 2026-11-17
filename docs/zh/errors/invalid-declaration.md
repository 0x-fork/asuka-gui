# invalid-declaration

声明的写法不被它所在的位置接受。

编译器报告的错误：修正之前程序无法构建。

## 原因

文件里的值不会改变（会变的值是 `state`），类型的常量写成 `let name = value`，组件的 `let` 只能命名一个值，默认值也不能读取还没创建出来的字段。

## 如何修正

按这个位置的写法来写：会变的值用 `state x = …`，常量用 `let name = value`。

## 错误示例

```asuka
var visits = 0
```

## 正确示例

```asuka
state visits = 0
```
