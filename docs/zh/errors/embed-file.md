# embed-file

`embed` 的文件不在包里。

编译器报告的错误：修正之前程序无法构建。

## 原因

`embed.text(path)` 等函数在编译时把文件读进应用：路径必须是字符串字面量，相对于包所在的目录，并且在目录之内。

## 如何修正

修正路径，或者把文件加到包里。

## 错误示例

```asuka
import std/embed

let terms = embed.text("terms.md")
```

## 正确示例

`app.asuka`

```asuka
import std/embed

let terms = embed.text("terms.md")
```

`terms.md`

```markdown
# Terms
```
