// std/store on Android: SharedPreferences.
package std.store

import android.content.Context
import dev.asuka.AsukaModules
import java.io.File

class SharedPrefs(private val context: Context) : Prefs {
    private val prefs = context.getSharedPreferences("asuka.store", Context.MODE_PRIVATE)
    override fun get(key: String): String? = prefs.getString(key, null)
    override fun set(key: String, value: String) { prefs.edit().putString(key, value).apply() }
    override fun remove(key: String) { prefs.edit().remove(key).apply() }
    // apply() writes on a thread of its own; commit() waits for it.
    override fun flush() { prefs.edit().commit() }
    override fun dir(): String = File(context.applicationInfo.dataDir, "shared_prefs").path
}

fun makePrefs(): Prefs = SharedPrefs(AsukaModules.context)
