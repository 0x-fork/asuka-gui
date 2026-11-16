// asuka/markdown on Android: Markwon (CommonMark, with GitHub's tables,
// task lists, strikethrough and autolinks) as spans in a TextView that
// isn't editable: one selection, links that open, the user's text size.
// Without Markwon's HTML plugin, raw HTML stays text.
package asuka.markdown

import android.content.Context
import android.text.method.LinkMovementMethod
import android.widget.TextView
import io.noties.markwon.Markwon
import io.noties.markwon.ext.strikethrough.StrikethroughPlugin
import io.noties.markwon.ext.tables.TablePlugin
import io.noties.markwon.ext.tasklist.TaskListPlugin
import io.noties.markwon.linkify.LinkifyPlugin
import android.view.View as AndroidView

private class MarkwonDocument(events: DocumentEvents) : Document {
    private val context: Context = events.context
    private val text = TextView(context).apply {
        setTextAppearance(com.google.android.material.R.style.TextAppearance_Material3_BodyLarge)
        setTextIsSelectable(true)
        movementMethod = LinkMovementMethod.getInstance()
        includeFontPadding = false
    }
    private val markwon: Markwon = Markwon.builder(context)
        .usePlugin(StrikethroughPlugin.create())
        .usePlugin(TablePlugin.create(context))
        .usePlugin(TaskListPlugin.create(context))
        .usePlugin(LinkifyPlugin.create())
        .build()
    private var source: String? = null

    override val view: AndroidView get() = text

    override fun setText(value: String) {
        if (value == source) return
        source = value
        markwon.setMarkdown(text, value)
    }
}

fun makeDocument(events: DocumentEvents): Document = MarkwonDocument(events)
