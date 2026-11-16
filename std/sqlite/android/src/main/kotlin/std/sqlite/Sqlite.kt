// std/sqlite on Android: where a database's relative name is (the
// app's files directory). SQLite itself is the plugin's library
// (libasqlite.so, in the APK), which the core calls.
package std.sqlite

import dev.asuka.AsukaModules

class AppPlace : Place {
    override fun dataDir(): String = AsukaModules.context.filesDir.path
}

fun makePlace(): Place = AppPlace()
