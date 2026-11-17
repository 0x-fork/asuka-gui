# import-cycle

相互导入的包。

编译器报告的错误：修正之前程序无法构建。

## 原因

一个包要在它导入的包之后检查，所以两个直接或间接相互导入的包无法确定检查顺序。

## 如何修正

把两者都需要的部分移到第三个包里，让它们都导入它。

## 错误示例

`app.asuka`

```asuka
import ./a

let n = a.one()
```

`a/a.asuka`

```asuka
import ../b

pub fn one() -> int = 1
pub fn two() -> int = b.two()
```

`b/b.asuka`

```asuka
import ../a

pub fn two() -> int = a.one() + 1
```

## 正确示例

`app.asuka`

```asuka
import ./a

let n = a.one()
```

`a/a.asuka`

```asuka
import ../b

pub fn one() -> int = 1
pub fn two() -> int = b.two()
```

`b/b.asuka`

```asuka
pub fn two() -> int = 2
```
