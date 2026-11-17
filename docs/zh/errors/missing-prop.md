# missing-prop

元素缺少它必需的属性。

编译器报告的错误：修正之前程序无法构建。

## 原因

没有默认值的 prop 在调用方给值之前是没有值的。

## 如何修正

在元素的代码块里设置它：`Badge { count: 3 }`，或者给这个 prop 一个默认值。

## 错误示例

```asuka
component Badge {
  prop count: int
  Text("{count}")
}

component Inbox {
  Badge()
}
```

## 正确示例

```asuka
component Badge {
  prop count: int
  Text("{count}")
}

component Inbox {
  Badge { count: 3 }
}
```
