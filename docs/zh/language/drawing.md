# 绘制

`Canvas(g => …)` 用一个 `Graphics` 来绘制，坐标以点为单位、从左上角算起。它在布局完成后绘制一次，尺寸或读取的内容变化时再重新绘制。`Shape(fn(width, height) -> Path)` 是一个由平台负责填充和描边的轮廓（`fill`、`stroke`、`lineWidth`、`trimEnd`、`dash` ……）。

```asuka
Canvas(g => {
  let ring = g.path().arc(g.width / 2, g.height / 2, 40, 0, 6.28318)
  g.stroke(ring, .accent, StrokeStyle(width: 8, cap: .round))
  g.text("{percent}%", g.width / 2, g.height / 2, CanvasText(size: 20, align: .center))
}) { height: 100 }
```

`Path` 通过它的方法构建，每个方法都返回它自己：`Path().move(x, y)`、`line`、`quad`、`cubic`、`rect`、`roundedRect`、`ellipse`、`arc(cx, cy, radius, start, end, clockwise: true)`（弧度，从 x 轴正方向起算）、`close()`；`g.path()` 是画布自己的一个空路径，每次绘制都会复用。`Graphics` 只在绘制函数运行期间有效：`width`、`height`、`fill(path, color)`（也可以传 `LinearGradient` 或 `RadialGradient`）、`stroke(path, color 或渐变, style)`、`save()`、`restore()`、`translate`、`scale`、`rotate(degrees)`、`opacity`、`clip(path)`、`transform(a, b, c, d, tx, ty)`、`evenOdd(on)`、`text(s, x, y, style)`。

`Shader(shader("""…"""))` 是一个由 GPU 程序绘制的方框。程序用 Kage 编写，随应用一起编译（程序里有错误时，由编译器报告），uniform 按名字设置（`uniforms: [Uniform(name: "Level", value: [0.5]), colorUniform("Tint", .accent)]`）。
