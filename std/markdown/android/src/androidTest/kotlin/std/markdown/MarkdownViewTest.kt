// The Android view (Markdown.kt's MarkdownView: a real TextView) given a
// reply streamed in frame by frame (the package's tests/stream.json): each
// frame writes over the text once at most (its edits caught by a
// TextWatcher), from where the first block that changed starts or after,
// the text before it as it was; a selection in the heading survives; and
// at the end the text and its spans are what a view given the whole reply
// has. cmd/asuka's TestStdAndroidViews runs it on a device.
package std.markdown

import android.text.Editable
import android.text.Selection
import android.text.Spanned
import android.text.TextWatcher
import android.text.style.BackgroundColorSpan
import android.text.style.CharacterStyle
import android.text.style.ForegroundColorSpan
import android.text.style.ParagraphStyle
import android.text.style.RelativeSizeSpan
import android.text.style.StyleSpan
import android.text.style.TypefaceSpan
import android.text.style.URLSpan
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MarkdownViewTest {
    private fun frames(): List<List<Paragraph>> {
        val ctx = InstrumentationRegistry.getInstrumentation().context
        val doc = JSONObject(ctx.assets.open("stream.json").bufferedReader().readText())
        val frames = doc.getJSONArray("frames")
        return List(frames.length()) { f ->
            val ps = frames.getJSONArray(f)
            List(ps.length()) { i ->
                val p = ps.getJSONObject(i)
                val runs = p.getJSONArray("runs")
                Paragraph(
                    runs = List(runs.length()) { k ->
                        val r = runs.getJSONObject(k)
                        Run(r.optString("text"), r.optBoolean("bold"), r.optBoolean("italic"), r.optBoolean("code"), r.optBoolean("struck"),
                            r.optBoolean("quoted"), r.optString("link"))
                    },
                    heading = p.optLong("heading"), indent = p.optLong("indent"), marker = p.optString("marker"), code = p.optBoolean("code"),
                    rule = p.optBoolean("rule"), row = p.optBoolean("row"), tight = p.optBoolean("tight"),
                )
            }
        }
    }

    /** The text's styling as words: each span's kind, place and what it sets. */
    private fun spans(s: Spanned): List<String> =
        s.getSpans(0, s.length, Any::class.java).filter { it is CharacterStyle || it is ParagraphStyle }.map {
            val what = when (it) {
                is StyleSpan -> "${it.style}"
                is URLSpan -> it.url
                is ForegroundColorSpan -> "${it.foregroundColor}"
                is BackgroundColorSpan -> "${it.backgroundColor}"
                is RelativeSizeSpan -> "${it.sizeChange}"
                is TypefaceSpan -> "${it.family}"
                else -> ""
            }
            "${it.javaClass.simpleName} ${s.getSpanStart(it)}-${s.getSpanEnd(it)} $what"
        }.sorted()

    @Test
    fun streamedReplyReplacesOnlyTheTail() {
        val inst = InstrumentationRegistry.getInstrumentation()
        val stream = frames()
        assertTrue("${stream.size} frames", stream.size > 20)
        inst.runOnMainSync {
            val v = MarkdownView(inst.targetContext)
            class Edit(val start: Int, val removed: Int, val added: Int)
            val edits = ArrayList<Edit>()
            v.text.addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence, start: Int, count: Int, after: Int) {
                    edits += Edit(start, count, after)
                }
                override fun onTextChanged(s: CharSequence, start: Int, before: Int, count: Int) {}
                override fun afterTextChanged(s: Editable) {}
            })
            var prev: List<Paragraph> = emptyList()
            var replaced = 0
            var shown = 0
            var selected = false
            for ((f, paras) in stream.withIndex()) {
                var first = 0
                while (first < prev.size && first < paras.size && prev[first] == paras[first]) first++
                val from = v.lengths.take(first).sum()
                val before = v.text.text.toString()
                edits.clear()
                v.show(paras)
                val now = v.text.text.toString()
                assertEquals("frame $f: blocks", paras.size, v.lengths.size)
                assertEquals("frame $f: the text isn't its blocks", v.lengths.sum(), now.length)
                assertTrue("frame $f: ${edits.size} edits", edits.size <= 1)
                if (first == prev.size && first == paras.size) assertTrue("frame $f: nothing changed, ${edits.size} edits", edits.isEmpty())
                edits.firstOrNull()?.let { e ->
                    assertTrue("frame $f: wrote from ${e.start}, want from $from (paragraph $first's start) or after", e.start >= from)
                    assertEquals("frame $f: wrote over to the end", before.length, e.start + e.removed)
                    assertEquals("frame $f: the text before the change changed", before.substring(0, e.start), now.substring(0, e.start))
                    replaced += e.added
                }
                shown += now.length
                prev = paras
                // Once the heading is whole and the next block has begun, part
                // of it is selected, as a reader would while the rest streams in.
                if (!selected && paras.size >= 2) {
                    selected = true
                    Selection.setSelection(v.text.editableText, 2, 6)
                }
            }
            val e = v.text.editableText
            assertTrue("the selection in the heading: ${Selection.getSelectionStart(e)}-${Selection.getSelectionEnd(e)}",
                selected && Selection.getSelectionStart(e) == 2 && Selection.getSelectionEnd(e) == 6)
            assertTrue("replaced $replaced characters of the $shown shown frame by frame", replaced * 4 < shown)

            // At the end, what a view given it all at once has.
            val whole = MarkdownView(inst.targetContext)
            whole.show(stream.last())
            assertEquals(whole.text.text.toString(), v.text.text.toString())
            assertTrue(v.text.text.startsWith("Moving the cache"))
            assertEquals(spans(whole.text.text as Spanned), spans(v.text.text as Spanned))
            android.util.Log.i("MarkdownViewTest", "markdown (android): ${stream.size} frames, $replaced characters replaced of $shown shown")
        }
    }
}
