# Drawing

`Canvas(g => …)` draws with a `Graphics`, in points from its top left,
once it's laid out, and again when its size or what it read changes;
`Shape(fn(width, height) -> Path)` is an outline the platform fills and
strokes (`fill`, `stroke`, `lineWidth`, `trimEnd`, `dash`, ...).

```asuka
Canvas(g => {
  let ring = g.path().arc(g.width / 2, g.height / 2, 40, 0, 6.28318)
  g.stroke(ring, .accent, StrokeStyle(width: 8, cap: .round))
  g.text("{percent}%", g.width / 2, g.height / 2, CanvasText(size: 20, align: .center))
}) { height: 100 }
```

A `Path` is built by its methods, which return it: `Path().move(x,
y)`, `line`, `quad`, `cubic`, `rect`, `roundedRect`, `ellipse`, `arc(cx,
cy, radius, start, end, clockwise: true)` (radians from the positive x
axis), `close()`; `g.path()` is an empty one of the canvas's own,
reused from draw to draw. A Graphics is valid while its draw runs:
`width`, `height`, `fill(path, color)` (or a `LinearGradient`, a
`RadialGradient`), `stroke(path, color or gradient, style)`, `save()`,
`restore()`, `translate`, `scale`, `rotate(degrees)`, `opacity`,
`clip(path)`, `transform(a, b, c, d, tx, ty)`, `evenOdd(on)`,
`text(s, x, y, style)`.

A `shader` declaration is a box a GPU program draws, written in
Asuka's syntax and compiled with the app (a mistake in it is a compile
error at its line), used as a view, its parameters its uniforms:

```asuka check
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

`fn fragment(pos: vec2) -> vec4` gives a pixel's color, premultiplied;
`size`, `scale` and `time` are the box's size in points, the pixels a
point and the seconds since it showed (`animated: true` runs it). A
parameter is a `float`, an `int`, a `bool` or a `Color`. See
[Shaders](../components.md#shaders) for the language a shader is
written in and where it runs.
