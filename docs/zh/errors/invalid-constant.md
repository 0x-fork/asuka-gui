# invalid-constant

编译时生成的值不是字面量，或者无法解析。

编译器报告的错误：修正之前程序无法构建。

## 原因

有些值在编译时由字符串字面量生成：SVG 图标来自它的标记，shader 的程序来自它的源码。运行时拼出来的文本做不到这一点，无法解析的标记也生成不了任何东西。

## 如何修正

直接传入字面量，并修正消息指出的无法解析的部分。

## 错误示例

```asuka
import std/svg

let icon = svg.Icon("<svg")
```

## 正确示例

```asuka
import std/svg

let icon = svg.Icon(r"""<svg viewBox="0 0 24 24"><path d="M12 3v12"/></svg>""")
```
