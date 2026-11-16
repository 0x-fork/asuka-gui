// std/markdown on Android: the paragraphs the core read (md4c) as
// spans in a TextView that isn't editable: one selection, links that
// open, the user's text size, Material 3's type and colors. A change
// writes over the text from the first block that differs (MarkdownView).
package std.markdown

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.text.Layout
import android.text.SpannableStringBuilder
import android.text.Selection
import android.text.Spanned
import android.text.method.LinkMovementMethod
import android.text.style.BackgroundColorSpan
import android.text.style.CharacterStyle
import android.text.style.ForegroundColorSpan
import android.text.style.LeadingMarginSpan
import android.text.style.LineBackgroundSpan
import android.text.style.ParagraphStyle
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

/** The text's colors and its list step, from the theme. */
internal class Look(context: Context, textSize: Float) {
    val label = MaterialColors.getColor(context, com.google.android.material.R.attr.colorOnSurface, 0xFF1D1B20.toInt())
    val secondary = MaterialColors.getColor(context, com.google.android.material.R.attr.colorOnSurfaceVariant, 0xFF49454F.toInt())
    val fill = MaterialColors.getColor(context, com.google.android.material.R.attr.colorSurfaceContainerHighest, 0xFFE6E0E9.toInt())
    val outline = MaterialColors.getColor(context, com.google.android.material.R.attr.colorOutlineVariant, 0xFFCAC4D0.toInt())
    val step = (textSize * 1.5f).toInt()
}

/**
 * Paragraph i of paras as styled text: after i > 0, the paragraph end that
 * parts it from the one before (and a half-height line when either isn't
 * tight).
 */
internal fun markdownBlock(paras: List<Paragraph>, i: Int, look: Look): SpannableStringBuilder {
    val p = paras[i]
    val out = SpannableStringBuilder()
    fun span(what: Any, from: Int) {
        out.setSpan(what, from, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }
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
        out.append('\u2003')
        span(BlockSpan(look.outline, rule = true), start)
        return out
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
            if (!p.code) span(BackgroundColorSpan(look.fill), from)
        }
        if (r.struck) span(StrikethroughSpan(), from)
        span(ForegroundColorSpan(if (r.quoted) look.secondary else look.label), from)
        if (r.link.isNotEmpty()) span(URLSpan(r.link), from)
    }
    if (out.length == start) out.append(' ') // an empty item: its marker still shown
    if (p.heading > 0) span(RelativeSizeSpan(headingSizes[(p.heading.toInt().coerceIn(1, 6)) - 1]), start)
    if (p.code) span(BlockSpan(look.fill, rule = false), start)
    if (p.indent > 0 || p.marker.isNotEmpty()) span(MarkerSpan(p.marker, look.step * p.indent.toInt(), look.step, look.secondary), start)
    if (p.row) for (k in 1..8) span(TabStopSpan.Standard(look.step * 2 * k), start)
    return out
}

/**
 * The paragraphs in a TextView that isn't editable, a block each, kept
 * while its paragraph is the same: a change (a reply streaming in) writes
 * over the text from the first block that differs to its end, in place
 * (the TextView's Editable), so the text before it keeps its layout and
 * its selection.
 */
internal class MarkdownView(private val context: Context) {
    val text = TextView(context).apply {
        setTextAppearance(com.google.android.material.R.style.TextAppearance_Material3_BodyLarge)
        setTextIsSelectable(true)
        movementMethod = LinkMovementMethod.getInstance()
        includeFontPadding = false
        setText("", TextView.BufferType.EDITABLE)
    }
    private var shown: List<Paragraph> = emptyList()
    /** Each block's length, in the text's characters. */
    var lengths: List<Int> = emptyList()
        private set

    /** Shows paras: where the text written over starts, or null when nothing changed. */
    fun show(paras: List<Paragraph>): Int? {
        var keep = 0
        while (keep < shown.size && keep < paras.size && shown[keep] == paras[keep]) keep++
        if (keep == shown.size && keep == paras.size) return null
        val look = Look(context, text.textSize)
        val start = lengths.take(keep).sum()
        val tail = SpannableStringBuilder()
        val next = lengths.take(keep).toMutableList()
        for (i in keep until paras.size) {
            val b = markdownBlock(paras, i, look)
            tail.append(b)
            next += b.length
        }
        val e = text.editableText
        val from = Selection.getSelectionStart(e)
        val to = Selection.getSelectionEnd(e)
        // The styles of what goes, first: replace stretches a span that
        // covers just the text it replaces over what replaces it.
        for (sp in e.getSpans(start, e.length, Any::class.java)) {
            if ((sp is CharacterStyle || sp is ParagraphStyle) && e.getSpanStart(sp) >= start) e.removeSpan(sp)
        }
        e.replace(start, e.length, tail)
        if (from >= 0) Selection.setSelection(e, minOf(from, start), minOf(to, start)) // what of it was before start
        shown = paras
        lengths = next
        return start
    }
}

private class SpansDocument(events: DocumentEvents) : Document {
    private val md = MarkdownView(events.context)

    override val view: AndroidView get() = md.text

    override fun setParagraphs(value: List<Paragraph>) {
        md.show(value)
    }
}

fun makeDocument(events: DocumentEvents): Document = SpansDocument(events)
