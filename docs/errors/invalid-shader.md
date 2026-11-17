# invalid-shader

A shader written with what the GPU's language doesn't have.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

A shader runs on the GPU: its values are numbers, vectors and matrices, its parameters uniforms (a float, an int, a bool or a Color), its code statements over them, and its entry `fn fragment(pos: vec2) -> vec4`.

## How to fix

Write it with numbers, vectors and GLSL's functions.

## Wrong

```asuka
shader Glow(label: string) {
  fn fragment(pos: vec2) -> vec4 {
    return vec4(1.0, 1.0, 1.0, 1.0)
  }
}
```

## Right

```asuka
shader Glow(strength: float) {
  fn fragment(pos: vec2) -> vec4 {
    return vec4(strength, strength, strength, 1.0)
  }
}
```
