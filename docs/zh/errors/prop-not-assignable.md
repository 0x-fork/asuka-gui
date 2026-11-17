# prop-not-assignable

组件给不能赋值的 prop 赋了值。

编译器报告的错误：修正之前程序无法构建。

## 原因

prop 属于调用方。只有当组件用 `event <prop>Change` 报告变化时才能给它赋值，这样绑定它的调用方（`bind open: shown`）才能拿到新值；而且 prop 只能整体改变。

## 如何修正

声明 `event openChange(open: bool)` 后再赋值，或者改用组件自己的状态。

## 错误示例

```asuka
component Disclosure {
  prop open = false
  Button("More") { on press { open = !open } }
}
```

## 正确示例

```asuka
component Disclosure {
  prop open = false
  event openChange(open: bool)
  Button("More") { on press { open = !open } }
}
```
