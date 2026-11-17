# deprecated-syntax

已被替换的旧写法，`asuka fmt` 会自动改写。

编译器报告的错误：修正之前程序无法构建。

## 原因

元素用 `style:` 应用样式，就像样式应用其他样式一样；在代码块里单独写样式名是旧的写法。

## 如何修正

运行 `asuka fmt -w`，或者写成 `style: name`。

## 错误示例

```asuka
style card {
  padding: 16
}

component Note {
  Column {
    style card
    Text("hi")
  }
}
```

## 正确示例

```asuka
style card {
  padding: 16
}

component Note {
  Column {
    style: card
    Text("hi")
  }
}
```
