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
