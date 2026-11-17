# invalid-shader

GPU の言語にないものを使ったシェーダー。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

シェーダーは GPU で動きます。値は数値、ベクトル、行列で、パラメーターは uniform（float、int、bool、Color）、コードはそれらに対する文で、入口は `fn fragment(pos: vec2) -> vec4` です。

## 直し方

数値、ベクトル、GLSL の関数で書きます。

## 誤った例

```asuka
shader Glow(label: string) {
  fn fragment(pos: vec2) -> vec4 {
    return vec4(1.0, 1.0, 1.0, 1.0)
  }
}
```

## 正しい例

```asuka
shader Glow(strength: float) {
  fn fragment(pos: vec2) -> vec4 {
    return vec4(strength, strength, strength, 1.0)
  }
}
```
