# not-pub

其他包（或 store、worker）没有公开的声明。

编译器报告的错误：修正之前程序无法构建。

## 原因

包里的声明默认只属于这个包，除非写了 `pub`；store 和 worker 的成员也是如此。这样包就可以自由修改别人没有用到的部分。

## 如何修正

在声明处加上 `pub`，或者改用这个包公开的内容。

## 错误示例

`app.asuka`

```asuka
import ./util

let answer = util.secret()
```

`util/util.asuka`

```asuka
fn secret() -> int = 42
```

## 正确示例

`app.asuka`

```asuka
import ./util

let answer = util.secret()
```

`util/util.asuka`

```asuka
pub fn secret() -> int = 42
```
