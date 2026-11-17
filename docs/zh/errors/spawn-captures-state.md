# spawn-captures-state

`spawn` 读取或修改了状态，或者用了不是值的东西。

编译器报告的错误：修正之前程序无法构建。

## 原因

`spawn` 在另一个线程上运行它的代码块，而主线程会继续修改状态；它只接收值（开始时复制一份），并返回一个值。

## 如何修正

先把状态读进一个 `let` 再传进去；把 `spawn` 的返回值赋给状态。

## 错误示例

```asuka
component Total {
  state items: [int] = []
  state sum = 0
  task {
    sum = await spawn { items.count() }
  }
  Text("{sum}")
}
```

## 正确示例

```asuka
component Total {
  state items: [int] = []
  state sum = 0
  task {
    let xs = items
    sum = await spawn { xs.count() }
  }
  Text("{sum}")
}
```
