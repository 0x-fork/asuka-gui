# 描画

`Canvas(g => …)` は `Graphics` を使って、左上を原点にポイント単位で描画します。レイアウトが済むと描画し、サイズや読み取った内容が変わると描き直します。`Shape(fn(width, height) -> Path)` は、プラットフォームが塗りと線を描く輪郭です（`fill`、`stroke`、`lineWidth`、`trimEnd`、`dash` など）。

```asuka
Canvas(g => {
  let ring = g.path().arc(g.width / 2, g.height / 2, 40, 0, 6.28318)
  g.stroke(ring, .accent, StrokeStyle(width: 8, cap: .round))
  g.text("{percent}%", g.width / 2, g.height / 2, CanvasText(size: 20, align: .center))
}) { height: 100 }
```

`Path` はメソッドで組み立てます。どのメソッドもパス自身を返します：`Path().move(x, y)`、`line`、`quad`、`cubic`、`rect`、`roundedRect`、`ellipse`、`arc(cx, cy, radius, start, end, clockwise: true)`（ラジアン、x 軸の正の向きから）、`close()`。`g.path()` はキャンバス自身が持つ空のパスで、描画のたびに使い回されます。`Graphics` が有効なのは描画関数の実行中だけです：`width`、`height`、`fill(path, color)`（`LinearGradient` や `RadialGradient` も渡せます）、`stroke(path, color またはグラデーション, style)`、`save()`、`restore()`、`translate`、`scale`、`rotate(degrees)`、`opacity`、`clip(path)`、`transform(a, b, c, d, tx, ty)`、`evenOdd(on)`、`text(s, x, y, style)`。

`shader` 宣言は、GPU のプログラムが描く箱です。Asuka の構文で書き、アプリと一緒にコンパイルされ（間違いはその行を示すコンパイルエラーになります）、ビューとして使います。引数がそのまま uniform になります。

```asuka
shader Ring(tint: Color, thickness: float = 0.05) {
  fn fragment(pos: vec2) -> vec4 {
    let d = abs(distance(pos / size, vec2(0.5)) - 0.4)
    return tint * smoothstep(thickness, 0, d)
  }
}

component Badge {
  Ring(tint: .accent) { width: 120; height: 120 }
}
```

`fn fragment(pos: vec2) -> vec4` はピクセルの色（プリマルチプライド）を返します。`size`、`scale`、`time` はそれぞれ枠のサイズ（ポイント）、1 ポイントあたりのピクセル数、表示されてからの秒数です（`animated: true` で進みます）。引数は `float`、`int`、`bool`、`Color` のいずれかです。シェーダーを書く言語とプラットフォームごとの動き方は[シェーダー](../components.md#シェーダー)を参照してください。
