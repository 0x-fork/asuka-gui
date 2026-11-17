# invalid-extern

C 无法支撑的 extern 声明。

编译器报告的错误：修正之前程序无法构建。

## 原因

extern fn 的名字是 C 函数的名字；extern type 的 `release` 必须是同一个包里接收一个该类型参数的 extern fn；extern struct 是 C 的内存布局，不需要声明别的东西。

## 如何修正

把释放函数声明为 `extern fn close(x: T)`，其余部分照 C 的样子声明。

## 错误示例

```asuka
extern type Handle with release close

fn close(h: Handle) {}
```

## 正确示例

```asuka
extern type Handle with release close

extern fn close(h: Handle)
```
