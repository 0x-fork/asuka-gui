package dev.asuka

import android.app.Application
import android.content.ContentProvider
import android.content.ContentValues
import android.content.pm.PackageManager
import android.database.Cursor
import android.net.Uri
import android.util.Log

/**
 * A native package's code that runs as the app's process starts, before
 * the app's own code: its manifest's `android.launch` names the class,
 * which the CLI lists in the app's manifest (AsukaLauncher's
 * dev.asuka.launch meta-data), so only the apps that import the package
 * (or use the member whose `[uses]` names it) run it. A process the
 * system started in the background (a receiver's broadcast, a job) runs
 * it too: what must listen from the start (Play Billing's purchases)
 * goes here. A broadcast that needs the app's code starts it itself
 * (AsukaRuntime.startHeadless).
 */
interface AsukaLaunching {
    /** Runs on the main thread, as the process starts. */
    fun launch(app: Application)
}

/**
 * Runs the app's packages' launch code (AsukaLaunching) as the process
 * starts: a content provider, made before Application.onCreate, which the
 * CLI declares only in the apps that have some.
 */
class AsukaLauncher : ContentProvider() {
    override fun onCreate(): Boolean {
        val app = context?.applicationContext as? Application ?: return false
        val names = try {
            app.packageManager.getApplicationInfo(app.packageName, PackageManager.GET_META_DATA).metaData?.getString("dev.asuka.launch")
        } catch (_: PackageManager.NameNotFoundException) {
            null
        } ?: return true
        for (name in names.split(',').map { it.trim() }.filter { it.isNotEmpty() }) {
            try {
                (Class.forName(name).getDeclaredConstructor().newInstance() as AsukaLaunching).launch(app)
            } catch (e: ReflectiveOperationException) {
                Log.e("asuka", "no launch class $name (a package's android.launch): $e")
            } catch (e: ClassCastException) {
                Log.e("asuka", "$name isn't an AsukaLaunching")
            }
        }
        return true
    }

    override fun query(uri: Uri, projection: Array<out String>?, selection: String?, selectionArgs: Array<out String>?, sortOrder: String?): Cursor? = null
    override fun getType(uri: Uri): String? = null
    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int = 0
    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?): Int = 0
}
