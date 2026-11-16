// The kitchen package's Kotlin: its module, on Android.
package _e2e.kitchen

private class KotlinKitchen : Kitchen {
    override fun name(): String = "home"

    override fun items(filter: String): List<Item> = listOf(Item(name = filter, weight = 1.5, unit = Unit.GRAM, tags = listOf("dry", "fine")))
}

fun makeKitchen(): Kitchen = KotlinKitchen()
