// asuka/shader on Android: the view's box drawn by the program's AGSL (a
// RuntimeShader, Android 13 and later; before, it's empty). Its uniforms
// are U0, U1..., set from the lanes the app's uniforms give; the view
// sets rectSize, pixelScale and Time. Animated, it draws again at each
// frame (postInvalidateOnAnimation).
package asuka.shader

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RuntimeShader
import android.os.Build
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import android.view.View as AndroidView

/** A program's code (internal/shadercode's Encode), as Android draws it. */
private class Code(b: ByteArray) {
    /** A uniform: its lanes, and their kind (0 float, 1 int, 2 uint, 3 bool). */
    class Var(val lane: Int, val lanes: Int, val kind: Int)

    var agsl = ""
    var lanes = 0
    var size = 0
    var scale = 0
    var time = -1
    val vars = ArrayList<Pair<String, Var>>() // in order: AGSL's U0, U1...

    init {
        val r = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN)
        fun str(): String = ByteArray(r.int).also { r.get(it) }.toString(Charsets.UTF_8)
        try {
            str() // the Kage source: the desktop's
            str() // Metal: Apple's
            agsl = str()
            lanes = r.int
            size = r.int
            scale = r.int
            time = r.int - 1
            r.int // the Metal buffer's size
            repeat(lanes) { r.int } // the Metal offsets
            repeat(r.int) {
                val name = str()
                val lane = r.int
                val n = r.int
                vars.add(name to Var(lane, n, if (r.hasRemaining()) r.get().toInt() else 0))
            }
        } catch (e: RuntimeException) {
            agsl = ""
        }
    }

    /** The lanes the app's uniforms set: a float's bits, an int's, a bool's 0 or 1. */
    fun words(us: List<Uniform>): IntArray {
        val w = IntArray(lanes)
        for (u in us) {
            if (u.name.startsWith("__")) continue
            val v = vars.firstOrNull { it.first == u.name }?.second ?: continue
            for (i in 0 until minOf(v.lanes, u.value.size)) {
                if (v.lane + i >= lanes) break
                val x = u.value[i]
                w[v.lane + i] = when (v.kind) {
                    0 -> java.lang.Float.floatToRawIntBits(x.toFloat())
                    1, 2 -> Math.round(x).toInt()
                    else -> if (x != 0.0) 1 else 0
                }
            }
        }
        return w
    }
}

internal class ShaderCanvas(context: Context) : AndroidView(context) {
    private var code: Code? = null
    private var uniforms: List<Uniform> = emptyList()
    private var words = IntArray(0)
    var animated = false
        set(v) { field = v; invalidate() }

    private var shader: Any? = null // a RuntimeShader, from Android 13
    private var failed = false
    private val paint = Paint()
    private val start = System.nanoTime()

    init { setWillNotDraw(false) }

    fun setProgram(p: Program) {
        val c = Code(p.code)
        code = c
        words = c.words(uniforms)
        shader = null
        failed = false
        invalidate()
    }

    fun setUniforms(us: List<Uniform>) {
        uniforms = us
        words = code?.words(us) ?: IntArray(0)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (Build.VERSION.SDK_INT < 33) return
        val c = code ?: return
        if (c.agsl.isEmpty() || width == 0 || height == 0 || words.size < c.lanes) return
        var s = shader as RuntimeShader?
        if (s == null && !failed) {
            try {
                s = RuntimeShader(c.agsl)
                shader = s
            } catch (e: IllegalArgumentException) {
                failed = true
                Log.e("asuka", "a shader's AGSL doesn't compile: ${e.message}")
            }
        }
        s ?: return
        val d = resources.displayMetrics.density
        words[c.size] = java.lang.Float.floatToRawIntBits(width / d)
        words[c.size + 1] = java.lang.Float.floatToRawIntBits(height / d)
        words[c.scale] = java.lang.Float.floatToRawIntBits(d)
        if (c.time >= 0) words[c.time] = java.lang.Float.floatToRawIntBits((System.nanoTime() - start) / 1e9f)
        for ((i, e) in c.vars.withIndex()) {
            val u = e.second
            try {
                // AGSL has no uints or bools: they're ints.
                if (u.kind != 0) s.setIntUniform("U$i", IntArray(u.lanes) { words[u.lane + it] })
                else s.setFloatUniform("U$i", FloatArray(u.lanes) { java.lang.Float.intBitsToFloat(words[u.lane + it]) })
            } catch (_: IllegalArgumentException) {
                // A uniform the program doesn't read: AGSL dropped it.
            }
        }
        paint.shader = s
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)
        if (animated && isAttachedToWindow) postInvalidateOnAnimation()
    }
}

private class AgslShader(events: ShaderEvents) : Shader {
    private val canvas = ShaderCanvas(events.context)
    override val view: AndroidView get() = canvas
    override fun setProgram(value: Program) = canvas.setProgram(value)
    override fun setUniforms(value: List<Uniform>) = canvas.setUniforms(value)
    override fun setAnimated(value: Boolean) { canvas.animated = value }
}

fun makeShader(events: ShaderEvents): Shader = AgslShader(events)
