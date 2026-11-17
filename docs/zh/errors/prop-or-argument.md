# prop-or-argument

参数、属性、事件或子视图放错了位置。

编译器报告的错误：修正之前程序无法构建。

## 原因

每样东西都有固定的位置：头部参数写在括号里，属性以 `name: value` 写在代码块里，事件写成 `on name { }`，子视图就是代码块里的视图。

## 如何修正

把它移到该在的位置：`Badge(3) { compact: true; on tap { … } }`。

## 错误示例

```asuka
component Badge {
  prop count = 0
  Text("{count}")
}

component Inbox {
  Badge(count: 3)
}
```

## 正确示例

```asuka
component Badge {
  prop count = 0
  Text("{count}")
}

component Inbox {
  Badge { count: 3 }
}
```
