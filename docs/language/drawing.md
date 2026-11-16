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

`Shader(shader("""…"""))` is a box a GPU program draws: the program is
Kage, compiled with the app (a mistake in it is the compiler's error),
its uniforms set by name (`uniforms: [Uniform(name: "Level", value:
[0.5]), colorUniform("Tint", .accent)]`).
