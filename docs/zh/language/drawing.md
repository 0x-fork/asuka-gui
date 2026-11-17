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

`shader` 声明是一个由 GPU 程序绘制的方框。它用 Asuka 的语法编写，随应用一起编译（写错了就是编译错误，并指出所在的行），用的时候当作视图，参数就是它的 uniform：

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

`fn fragment(pos: vec2) -> vec4` 给出一个像素的颜色（预乘 alpha）；`size`、`scale` 和 `time` 分别是框的尺寸（点）、每个点的像素数和显示以来的秒数（`animated: true` 让它走起来）。参数可以是 `float`、`int`、`bool` 或 `Color`。着色器所用的语言和它在各平台上怎么运行，见[着色器](../components.md#着色器)。
