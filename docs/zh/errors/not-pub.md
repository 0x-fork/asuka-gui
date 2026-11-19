# not-pub

其他包、其他文件（private）、store 或 worker 没有公开的声明。

编译器报告的错误：修正之前程序无法构建。

## 原因

包里的声明默认只属于这个包，除非写了 `pub`；store 和 worker 的成员也是如此；而 `private` 的东西（声明，或类型的字段和方法）只属于它所在的文件，以及这个包的测试。这样包就可以自由修改别人没有用到的部分，类型的不变式也只由它自己维护。

## 如何修正

在声明处加上 `pub`（或去掉 `private`），或者改用公开的内容：私有字段没有默认值的类型，用它的 static fn 来创建。

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
