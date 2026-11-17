# misplaced-declaration

声明出现在它这一类不能出现的地方。

编译器报告的错误：修正之前程序无法构建。

## 原因

每个地方只能包含特定的声明：文件先写导入，然后是类型、函数和状态；struct 包含字段和方法；store 包含状态、let、fn、task 和 effect；worker 包含 var、let 和 fn；enum 先写 case，再写常量和方法；扩展包含方法和常量。

## 如何修正

把它移到这一类声明该在的地方，或者改用这里接受的写法（在 worker 里用 `var` 代替 `state`）。

## 错误示例

```asuka
worker Index {
  state count = 0
}
```

## 正确示例

```asuka
worker Index {
  var count = 0
}
```
