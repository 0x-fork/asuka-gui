// asuka/svg on Android: the icon's layers (the compiler's encoding:
// sdk/svg's Encode) as Paths, drawn on a view's Canvas, fitted to it;
// currentColor is the view's color, from the app's theme.
package asuka.svg

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import dev.asuka.AsukaModules
import java.nio.ByteBuffer
import java.nio.ByteOrder
import android.view.View as AndroidView

/** An icon's layers, read from its data. */
internal class Drawing(data: ByteArray) {
    class Layer(val fill: Long, val fillRGBA: Int, val fillOpacity: Float, val stroke: Long, val strokeRGBA: Int, val strokeOpacity: Float,
                val lineWidth: Float, val cap: Paint.Cap, val join: Paint.Join, val path: Path)

    var vx = 0f; var vy = 0f; var vw = 0f; var vh = 0f
    var width = 0f; var height = 0f
    val layers = ArrayList<Layer>()

    init {
        val b = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        if (data.size >= 24) {
            vx = b.float; vy = b.float; vw = b.float; vh = b.float
            width = b.float; height = b.float
            val n = b.int
            repeat(n) {
                if (b.remaining() < 2) return@repeat
                val (fk, frgba, fo) = paint(b)
                val (sk, srgba, so) = paint(b)
                val lw = b.float
                val cap = arrayOf(Paint.Cap.BUTT, Paint.Cap.ROUND, Paint.Cap.SQUARE)[minOf(b.get().toInt(), 2)]
                val join = arrayOf(Paint.Join.MITER, Paint.Join.ROUND, Paint.Join.BEVEL)[minOf(b.get().toInt(), 2)]
                val end = b.position() + b.int
                val p = Path()
                while (b.position() < minOf(end, data.size)) {
                    when (b.get().toInt()) {
                        1 -> p.moveTo(b.float, b.float)
                        2 -> p.lineTo(b.float, b.float)
                        3 -> p.quadTo(b.float, b.float, b.float, b.float)
                        4 -> p.cubicTo(b.float, b.float, b.float, b.float, b.float, b.float)
                        5 -> p.close()
                        6 -> { val x = b.float; val y = b.float; p.addRect(x, y, x + b.float, y + b.float, Path.Direction.CW) }
                        7 -> {
                            val x = b.float; val y = b.float; val w = b.float; val h = b.float; val r = b.float
                            p.addRoundRect(RectF(x, y, x + w, y + h), minOf(r, w / 2), minOf(r, h / 2), Path.Direction.CW)
                        }
                        8 -> { val x = b.float; val y = b.float; p.addOval(RectF(x, y, x + b.float, y + b.float), Path.Direction.CW) }
                        else -> b.position(end)
                    }
                }
                b.position(minOf(end, data.size))
                layers.add(Layer(fk, frgba, fo, sk, srgba, so, lw, cap, join, p))
            }
        }
    }

    /** A paint: its kind (0 none, 1 a color, 2 currentColor), its ARGB, its opacity. */
    private fun paint(b: ByteBuffer): Triple<Long, Int, Float> = when (b.get().toInt()) {
        1 -> {
            val r = b.get().toInt() and 255; val g = b.get().toInt() and 255; val bl = b.get().toInt() and 255; val a = b.get().toInt() and 255
            Triple(1L, (a shl 24) or (r shl 16) or (g shl 8) or bl, 1f)
        }
        2 -> Triple(2L, 0, b.float)
        else -> Triple(0L, 0, 0f)
    }
}

internal class IconView(context: Context) : AndroidView(context) {
    var drawing = Drawing(ByteArray(0))
        set(v) { field = v; requestLayout(); invalidate() }
    var color = 0L
        set(v) { field = v; invalidate() }
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

    override fun onMeasure(w: Int, h: Int) {
        val d = resources.displayMetrics.density
        setMeasuredDimension(resolveSize((drawing.width * d).toInt(), w), resolveSize((drawing.height * d).toInt(), h))
    }

    override fun onDraw(c: Canvas) {
        val d = drawing
        if (d.vw <= 0 || d.vh <= 0) return
        val scale = minOf(width / d.vw, height / d.vh)
        val dx = (width - d.vw * scale) / 2 - d.vx * scale
        val dy = (height - d.vh * scale) / 2 - d.vy * scale
        val current = AsukaModules.color(color, 0xff000000.toInt())
        fun argb(kind: Long, rgba: Int, o: Float): Int? = when (kind) {
            1L -> rgba
            2L -> {
                val a = ((current ushr 24) * o.coerceIn(0f, 1f)).toInt()
                (a shl 24) or (current and 0xffffff)
            }
            else -> null
        }
        c.save()
        c.translate(dx, dy)
        c.scale(scale, scale)
        for (l in d.layers) {
            argb(l.fill, l.fillRGBA, l.fillOpacity)?.let {
                paint.style = Paint.Style.FILL
                paint.color = it
                c.drawPath(l.path, paint)
            }
            argb(l.stroke, l.strokeRGBA, l.strokeOpacity)?.let {
                if (l.lineWidth > 0) {
                    paint.style = Paint.Style.STROKE
                    paint.color = it
                    paint.strokeWidth = l.lineWidth
                    paint.strokeCap = l.cap
                    paint.strokeJoin = l.join
                    c.drawPath(l.path, paint)
                }
            }
        }
        c.restore()
    }
}

private class SvgView(events: ViewEvents) : View {
    private val icon = IconView(events.context)
    override val view: AndroidView get() = icon
    override fun setIcon(value: Icon) { icon.drawing = Drawing(value.data) }
    override fun setColor(value: Long) { icon.color = value }
}

fun makeView(events: ViewEvents): View = SvgView(events)

// Shape: the app's paths, resolved in the view's size each time it draws
// (a point is a fraction of the box plus points, times the density).

internal class ShapeCanvas(context: Context) : AndroidView(context) {
    var layers: List<Layer> = emptyList()
        set(v) { field = v; invalidate() }
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val path = Path()

    private fun build(d: List<Double>, w: Float, h: Float, density: Float) {
        path.reset()
        var i = 0
        val pt = FloatArray(2)
        fun next(): FloatArray {
            if (i + 4 > d.size) { i = d.size; pt[0] = 0f; pt[1] = 0f; return pt }
            pt[0] = (d[i] * w + d[i + 1] * density).toFloat()
            pt[1] = (d[i + 2] * h + d[i + 3] * density).toFloat()
            i += 4
            return pt
        }
        while (i < d.size) {
            val op = d[i].toInt()
            i++
            when (op) {
                1 -> next().let { path.moveTo(it[0], it[1]) }
                2 -> next().let { path.lineTo(it[0], it[1]) }
                3 -> {
                    val (cx, cy) = next().let { it[0] to it[1] }
                    next().let { path.quadTo(cx, cy, it[0], it[1]) }
                }
                4 -> {
                    val (ax, ay) = next().let { it[0] to it[1] }
                    val (bx, by) = next().let { it[0] to it[1] }
                    next().let { path.cubicTo(ax, ay, bx, by, it[0], it[1]) }
                }
                5 -> path.close()
                6, 7 -> {
                    val (ax, ay) = next().let { it[0] to it[1] }
                    val (bx, by) = next().let { it[0] to it[1] }
                    val r = RectF(minOf(ax, bx), minOf(ay, by), maxOf(ax, bx), maxOf(ay, by))
                    if (op == 6) {
                        val rad = if (i < d.size) (d[i] * density).toFloat() else 0f
                        i++
                        val k = rad.coerceIn(0f, minOf(r.width(), r.height()) / 2)
                        path.addRoundRect(r, k, k, Path.Direction.CW)
                    } else {
                        path.addOval(r, Path.Direction.CW)
                    }
                }
                else -> return
            }
        }
    }

    override fun onDraw(c: Canvas) {
        val density = resources.displayMetrics.density
        val w = width.toFloat()
        val h = height.toFloat()
        for (l in layers) {
            build(l.path.data, w, h, density)
            l.fill?.let { f ->
                paint.style = Paint.Style.FILL
                val top = AsukaModules.color(f, 0)
                val fade = l.fade
                if (fade != null) {
                    paint.shader = android.graphics.LinearGradient(0f, 0f, 0f, h, top, AsukaModules.color(fade, 0), android.graphics.Shader.TileMode.CLAMP)
                } else {
                    paint.color = top
                }
                c.drawPath(path, paint)
                paint.shader = null
            }
            val s = l.stroke
            if (s != null && l.width > 0) {
                paint.style = Paint.Style.STROKE
                paint.color = AsukaModules.color(s, 0)
                paint.strokeWidth = (l.width * density).toFloat()
                paint.strokeCap = when (l.cap.value) { 1L -> Paint.Cap.ROUND; 2L -> Paint.Cap.SQUARE; else -> Paint.Cap.BUTT }
                paint.strokeJoin = when (l.join.value) { 1L -> Paint.Join.ROUND; 2L -> Paint.Join.BEVEL; else -> Paint.Join.MITER }
                c.drawPath(path, paint)
            }
        }
    }
}

private class SvgShape(events: ShapeEvents) : Shape {
    private val canvas = ShapeCanvas(events.context)
    override val view: AndroidView get() = canvas
    override fun setLayers(value: List<Layer>) { canvas.layers = value }
}

fun makeShape(events: ShapeEvents): Shape = SvgShape(events)
