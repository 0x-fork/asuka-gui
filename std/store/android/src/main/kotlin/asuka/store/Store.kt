// asuka/store on Android: SharedPreferences.
package asuka.store

import android.content.Context
import dev.asuka.AsukaModules

class SharedPrefs(context: Context) : Prefs {
    private val prefs = context.getSharedPreferences("asuka.store", Context.MODE_PRIVATE)
    override fun get(key: String): String? = prefs.getString(key, null)
    override fun set(key: String, value: String) { prefs.edit().putString(key, value).apply() }
    override fun remove(key: String) { prefs.edit().remove(key).apply() }
}

fun makePrefs(): Prefs = SharedPrefs(AsukaModules.context)
