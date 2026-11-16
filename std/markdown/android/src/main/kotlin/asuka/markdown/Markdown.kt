// asuka/markdown on Android: the paragraphs the core read (md4c) as
// spans in a TextView that isn't editable: one selection, links that
// open, the user's text size, Material 3's type and colors.
package asuka.markdown

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.text.Layout
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.method.LinkMovementMethod
import android.text.style.BackgroundColorSpan
import android.text.style.ForegroundColorSpan
import android.text.style.LeadingMarginSpan
import android.text.style.LineBackgroundSpan
import android.text.style.RelativeSizeSpan
import android.text.style.StrikethroughSpan
import android.text.style.StyleSpan
import android.text.style.TabStopSpan
import android.text.style.TypefaceSpan
import android.text.style.URLSpan
import android.widget.TextView
import com.google.android.material.color.MaterialColors
import android.view.View as AndroidView

private val headingSizes = floatArrayOf(1.6f, 1.35f, 1.15f, 1f, 1f, 1f)

/** Indents a paragraph steps in, drawing its list marker in the step before its first line. */
private class MarkerSpan(private val marker: String, private val margin: Int, private val step: Int, private val color: Int) :
    LeadingMarginSpan {
    override fun getLeadingMargin(first: Boolean): Int = margin

    override fun drawLeadingMargin(
        c: Canvas, p: Paint, x: Int, dir: Int, top: Int, baseline: Int, bottom: Int,
        text: CharSequence, start: Int, end: Int, first: Boolean, layout: Layout?,
    ) {
        if (marker.isEmpty() || (text as? Spanned)?.getSpanStart(this) != start) return
        val saved = p.color
        p.color = color
        val at = (margin - step).toFloat()
        val w = p.measureText(marker)
        c.drawText(marker, if (dir > 0) x + at else x - at - w, baseline.toFloat(), p)
        p.color = saved
    }
}

/** Fills a code block's lines across, or draws a thematic break's line. */
private class BlockSpan(private val color: Int, private val rule: Boolean) : LineBackgroundSpan {
    override fun drawBackground(
        c: Canvas, p: Paint, left: Int, right: Int, top: Int, baseline: Int, bottom: Int,
        text: CharSequence, start: Int, end: Int, line: Int,
    ) {
        val saved = p.color
        p.color = color
        if (rule) {
            val y = (top + bottom) / 2f
            c.drawRect(left.toFloat(), y, right.toFloat(), y + maxOf(1f, p.textSize / 16), p)
        } else {
            c.drawRect(left.toFloat(), top.toFloat(), right.toFloat(), bottom.toFloat(), p)
        }
        p.color = saved
    }
}

/** The paragraphs as styled text. */
internal fun markdownText(context: Context, paras: List<Paragraph>, textSize: Float): CharSequence {
    val label = MaterialColors.getColor(context, com.google.android.material.R.attr.colorOnSurface, 0xFF1D1B20.toInt())
    val secondary = MaterialColors.getColor(context, com.google.android.material.R.attr.colorOnSurfaceVariant, 0xFF49454F.toInt())
    val fill = MaterialColors.getColor(context, com.google.android.material.R.attr.colorSurfaceContainerHighest, 0xFFE6E0E9.toInt())
    val outline = MaterialColors.getColor(context, com.google.android.material.R.attr.colorOutlineVariant, 0xFFCAC4D0.toInt())
    val step = (textSize * 1.5f).toInt()
    val out = SpannableStringBuilder()
    fun span(what: Any, from: Int) {
        out.setSpan(what, from, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }
    for ((i, p) in paras.withIndex()) {
        if (i > 0) {
            out.append('\n')
            if (!paras[i - 1].tight || !p.tight) {
                val gap = out.length
                out.append('\n')
                span(RelativeSizeSpan(0.5f), gap)
            }
        }
        val start = out.length
        if (p.rule) {
            out.append(' ')
            span(BlockSpan(outline, rule = true), start)
            continue
        }
        for (r in p.runs) {
            val from = out.length
            out.append(r.text)
            if (out.length == from) continue
            val bold = r.bold || p.heading > 0
            when {
                bold && r.italic -> span(StyleSpan(Typeface.BOLD_ITALIC), from)
                bold -> span(StyleSpan(Typeface.BOLD), from)
                r.italic -> span(StyleSpan(Typeface.ITALIC), from)
            }
            if (r.code || p.code) {
                span(TypefaceSpan("monospace"), from)
                span(RelativeSizeSpan(0.9f), from)
                if (!p.code) span(BackgroundColorSpan(fill), from)
            }
            if (r.struck) span(StrikethroughSpan(), from)
            span(ForegroundColorSpan(if (r.quoted) secondary else label), from)
            if (r.link.isNotEmpty()) span(URLSpan(r.link), from)
        }
        if (out.length == start) out.append(' ') // an empty item: its marker still shown
        if (p.heading > 0) span(RelativeSizeSpan(headingSizes[(p.heading.toInt().coerceIn(1, 6)) - 1]), start)
        if (p.code) span(BlockSpan(fill, rule = false), start)
        if (p.indent > 0 || p.marker.isNotEmpty()) {
            span(MarkerSpan(p.marker, step * p.indent.toInt(), step, secondary), start)
        }
        if (p.row) for (k in 1..8) span(TabStopSpan.Standard(step * 2 * k), start)
    }
    return out
}

private class SpansDocument(events: DocumentEvents) : Document {
    private val context: Context = events.context
    private val text = TextView(context).apply {
        setTextAppearance(com.google.android.material.R.style.TextAppearance_Material3_BodyLarge)
        setTextIsSelectable(true)
        movementMethod = LinkMovementMethod.getInstance()
        includeFontPadding = false
    }
    private var shown: List<Paragraph>? = null

    override val view: AndroidView get() = text

    override fun setParagraphs(value: List<Paragraph>) {
        if (value == shown) return // as it is: its selection kept
        shown = value
        text.text = markdownText(context, value, text.textSize)
    }
}

fun makeDocument(events: DocumentEvents): Document = SpansDocument(events)
