# invalid-shader

shader 里用了 GPU 语言没有的东西。

编译器报告的错误：修正之前程序无法构建。

## 原因

shader 运行在 GPU 上：它的值是数字、向量和矩阵，参数是 uniform（float、int、bool 或 Color），代码是作用于这些值的语句，入口是 `fn fragment(pos: vec2) -> vec4`。

## 如何修正

用数字、向量和 GLSL 的函数来写。

## 错误示例

```asuka
shader Glow(label: string) {
  fn fragment(pos: vec2) -> vec4 {
    return vec4(1.0, 1.0, 1.0, 1.0)
  }
}
```

## 正确示例

```asuka
shader Glow(strength: float) {
  fn fragment(pos: vec2) -> vec4 {
    return vec4(strength, strength, strength, 1.0)
  }
}
```
