// asuka/preview on Android: no system viewer, nor one that pages through
// several files, so the start file opens in the app the user picks for
// its type (ACTION_VIEW), through the app's FileProvider (the host's),
// readable by that app alone, as Android's mail apps open an attachment;
// it returns once the user is back.
package asuka.preview

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.webkit.MimeTypeMap
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.FileProvider
import dev.asuka.AsukaModules
import java.io.File
import java.util.UUID
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class AppViewer(private val app: Context) : Viewer {
    override suspend fun show(files: List<String>, start: Long, edit: Boolean) {
        val file = AsukaModules.localFile(files[start.toInt().coerceIn(0, files.size - 1)])
        val uri = withContext(Dispatchers.IO) { providerURI(file) }
        val type = MimeTypeMap.getSingleton().getMimeTypeFromExtension(file.extension.lowercase()) ?: "application/octet-stream"
        val view = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, type)
            clipData = ClipData.newRawUri(file.name, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        try {
            AsukaModules.launch(ActivityResultContracts.StartActivityForResult(), view)
        } catch (e: ActivityNotFoundException) {
            throw PreviewException(PreviewError.NO_VIEWER)
        }
    }

    /** The file's content URI, through the app's FileProvider: a file outside its folders is copied to its caches first. */
    private fun providerURI(f: File): Uri {
        val authority = app.packageName + ".asuka.files"
        return try {
            FileProvider.getUriForFile(app, authority, f)
        } catch (_: IllegalArgumentException) {
            val c = File(File(app.cacheDir, "asuka-share/" + UUID.randomUUID()).apply { mkdirs() }, f.name)
            f.copyTo(c, overwrite = true)
            FileProvider.getUriForFile(app, authority, c)
        }
    }
}

fun makeViewer(): Viewer = AppViewer(AsukaModules.context)
