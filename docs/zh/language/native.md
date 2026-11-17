# 原生代码

只有平台才能做的事，放在原生包里：用 Asuka 声明，再用各平台自己的语言实现：

```asuka
pub native interface Haptics {
  fn impact(style: Feedback)
}

pub native component MapView(region: Region) {
  prop pins: [Pin] = []
  event regionChanged(region: Region)
}
```

创建模块和调用它都可能失败（当前平台可能没有它的代码），所以两者都要加 `try`：`let h = try Haptics()`、`try h.impact(.light)`。`asuka native generate` 会生成各平台需要实现的 Swift、Kotlin、JavaScript 和 C 代码。详见[原生代码](../native.md)。

C 则可以直接调用。`extern` 声明用 C 的类型写明一个 C 函数接收什么、返回什么，Asuka 把每个声明都当作自己的函数来看待（`Int32` 就是 `int`，`CString` 就是 `string`）：

```asuka check
extern type Db with release db_close
extern fn db_open(path: CString) -> Db?
extern fn db_close(db: Db)
extern fn db_each(db: Db, sql: CString, row: fn(CString?) -> bool) -> Int32

extern struct Point {
  x: Float32
  y: Float32
}
```

`extern type` 是 C 给出的一个指针，最后一个引用消失时由它的 release 函数释放；`extern struct` 按值传递；传给 C 的函数就是回调。`asuka.toml` 的 `[c]` 写明应用要编译和链接哪些 C 代码。详见[在 Asuka 中调用 C](../c.md)。
