// std/sqlite on Android: where a database's relative name is (the
// app's files directory). SQLite itself is std/sqlite's C (its
// [c]), linked into the app's libasuka.so, which the core calls.
package std.sqlite

import dev.asuka.AsukaModules

class AppPlace : Place {
    override fun dataDir(): String = AsukaModules.context.filesDir.path
}

fun makePlace(): Place = AppPlace()
