# invalid-pattern

言語が受け付けない形で書かれたパターン。

コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。

## 理由

パターンには決まった形があります。case は `.name(…)` か `Type.name(…)`、struct のパターンはフィールド名を書き（`Point(x: 0)`）、`|` でつないだパターンは名前を束縛できず、`let` は名前を束縛するだけで判定はせず、`for` は名前か名前のタプルを束縛します。

## 直し方

メッセージが示す形で書きます（`.case(x)`、`if let`、`(k, v)`）。

## 誤った例

```asuka
enum Shape {
  circle(radius: float)
  square(side: float)
}

fn size(s: Shape) -> float = match s {
  .circle(x) | .square(x) => x
}
```

## 正しい例

```asuka
enum Shape {
  circle(radius: float)
  square(side: float)
}

fn size(s: Shape) -> float = match s {
  .circle(r) => r
  .square(side) => side
}
```
