# extension-conflict

扩展添加了类型已有的名称，或者另一个扩展也添加了同样的名称。

编译器报告的错误：修正之前程序无法构建。

## 原因

扩展只能添加名称，不能替换已有的名称，这样一个方法的含义就不会因为导入了什么而改变。两个扩展给同一个类型添加同一个名称，会让每一次调用都产生歧义。

## 如何修正

给方法换个名字，或者只保留其中一个扩展。

## 错误示例

```asuka
extend string {
  fn count() -> int = 0
}
```

## 正确示例

```asuka
extend string {
  fn wordCount() -> int = fields().count()
}
```
