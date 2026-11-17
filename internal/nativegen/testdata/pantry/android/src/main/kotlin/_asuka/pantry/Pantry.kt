// The pantry package's Kotlin on Android: what pantry.asuka declares,
// implemented (the GUI tests' plugin app calls it).
package _asuka.pantry

import android.view.View
import android.widget.SeekBar
import dev.asuka.AsukaModules
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.delay

private class KotlinPantry : Pantry {
    override fun name() = "home"
    override fun items(filter: String) = listOf(
        Item(name = filter, weight = 1.5, level = Level.HIGH, tags = listOf("a", "b"), notes = mapOf("k" to "v"),
            next = Item(name = "inner"), data = byteArrayOf(1, 2, 3), took = 2.5.seconds, count = -3, fresh = true),
    )
    override fun add(item: Item) {
        if (item.name.isEmpty()) throw PantryException(PantryError.BUSY)
    }
    override suspend fun cook(recipe: String, progress: (Double) -> kotlin.Unit): Item {
        if (recipe == "slow") delay(10_000)
        progress(0.5)
        progress(1.0)
        return Item(name = "$recipe done")
    }
    override fun ask(question: (String) -> Boolean) = question("ok?")
    override fun open(name: String): Shelf {
        if (name == "none") throw IllegalStateException("no shelf")
        return AsukaModules.onMain { KotlinShelf() }
    }
    override fun show() {}
    override fun counts() = mapOf("a" to 1L, "b" to -2L)
    override fun total(prices: List<java.math.BigDecimal>) = prices.fold(java.math.BigDecimal.ZERO) { a, b -> a + b }
    override fun shows(view: dev.asuka.AsukaViewRef) = AsukaModules.onMain { view.view != null }
}

private class KotlinShelf : Shelf {
    override fun stack(count: Long, level: Level) {
        if (level == Level.HIGH && count > 10) throw PantryException(PantryError.EMPTY)
    }
    override fun close() {}
}

fun makePantry(): Pantry = KotlinPantry()
fun makeShelf(): Shelf = KotlinShelf()

private class KotlinDial(private val events: DialEvents) : Dial {
    private val bar = SeekBar(events.context).apply { max = 100 }
    override val view: View get() = bar
    override fun setValue(value: Double) {
        bar.progress = (value * 100).toInt()
        events.changed(value * 2)
    }
    override fun setLabel(value: String) { bar.contentDescription = value }
    override fun setItems(value: List<Item>) {
        value.lastOrNull()?.let { events.picked(it, (value.size - 1).toLong()) }
    }
}

fun makeDial(events: DialEvents): Dial = KotlinDial(events)
