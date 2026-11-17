// platform/print on Android: PrintManager's print screen (its "Save as PDF"
// a printer too) for PDFs (as they are), images (fit to the page, as
// PrintHelper prints them), markup (a WebView that's never shown,
// paginated as it prints) and a WebView's page (its own print adapter);
// a PDF of markup written by the WebView's print adapter without the
// screen (android.print.AsukaPdfWriter, the host's).
package platform.print

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.pdf.PdfRenderer
import android.os.Bundle
import android.os.CancellationSignal
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.print.AsukaPdfWriter
import android.print.PageRange
import android.print.PrintAttributes
import android.print.PrintDocumentAdapter
import android.print.PrintDocumentInfo
import android.print.PrintJob
import android.print.PrintJobInfo
import android.print.PrintManager
import android.print.pdf.PrintedPdfDocument
import android.webkit.MimeTypeMap
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.exifinterface.media.ExifInterface
import dev.asuka.AsukaModules
import dev.asuka.AsukaUnsupportedException
import java.io.File
import java.io.FileOutputStream
import java.util.UUID
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext

class AndroidPrinter(private val app: Context) : Printer {
    private val appName get() = app.applicationInfo.loadLabel(app.packageManager).toString()

    override suspend fun file(source: String, job: Job): Outcome {
        val f = AsukaModules.localFile(source)
        val mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(f.extension.lowercase()) ?: ""
        val name = job.name.ifEmpty { f.name }
        return when {
            mime == "application/pdf" -> print(name, AsukaPdfFileAdapter(f, name), job)
            mime.startsWith("image/") -> {
                val bmp = withContext(Dispatchers.IO) { runCatching { printBitmap(f) }.getOrNull() } ?: throw PrintException(PrintError.UNSUPPORTED)
                // Turned for an image wider than tall, as PrintHelper does.
                val turned = if (bmp.width > bmp.height) job.copy(landscape = true) else job
                print(name, AsukaImageAdapter(app, bmp, name), turned, photo = true)
            }
            else -> throw PrintException(PrintError.UNSUPPORTED)
        }
    }

    override suspend fun html(markup: String, job: Job): Outcome = withContext(Dispatchers.Main) {
        val web = loadPage(markup)
        val name = job.name.ifEmpty { web.title?.takeIf { it.isNotBlank() && it != "about:blank" } ?: appName }
        try {
            print(name, web.createPrintDocumentAdapter(name), job)
        } finally {
            web.destroy()
        }
    }

    override suspend fun webView(view: dev.asuka.AsukaViewRef, job: Job): Outcome = withContext(Dispatchers.Main) {
        val web = findWeb(view.view) ?: throw PrintException(PrintError.UNSUPPORTED)
        val name = job.name.ifEmpty { web.title?.takeIf { it.isNotBlank() && it != "about:blank" } ?: appName }
        print(name, web.createPrintDocumentAdapter(name), job)
    }

    /** The WebView a view is, or holds. */
    private fun findWeb(v: android.view.View?): WebView? = when (v) {
        null -> null
        is WebView -> v
        is android.view.ViewGroup -> (0 until v.childCount).firstNotNullOfOrNull { findWeb(v.getChildAt(it)) }
        else -> null
    }

    override suspend fun pdf(layout: Layout): String = withContext(Dispatchers.Main) {
        fun mils(pt: Double) = (pt / 72 * 1000).toInt()
        val attrs = PrintAttributes.Builder()
            .setMediaSize(PrintAttributes.MediaSize("asuka", "asuka", mils(layout.width), mils(layout.height)))
            .setResolution(PrintAttributes.Resolution("pdf", "pdf", 600, 600))
            .setMinMargins(PrintAttributes.Margins(mils(layout.left), mils(layout.top), mils(layout.right), mils(layout.bottom)))
            .setColorMode(PrintAttributes.COLOR_MODE_COLOR)
            .build()
        val web = loadPage(layout.html)
        try {
            // Named after the page's title, as it's shared or mailed, in a folder of its own.
            val title = (web.title?.takeIf { it != "about:blank" } ?: "").replace('/', '-').replace(':', '-').trim().ifEmpty { "Document" }
            val f = File(app.cacheDir, "asuka-print/" + UUID.randomUUID() + "/" + title + ".pdf")
            f.parentFile?.mkdirs()
            suspendCancellableCoroutine<kotlin.Unit> { cont ->
                AsukaPdfWriter.write(web.createPrintDocumentAdapter(appName), attrs, f) { e ->
                    if (e == null) cont.resume(kotlin.Unit) else cont.resumeWithException(e)
                }
            }
            f.absolutePath
        } finally {
            web.destroy()
        }
    }

    /** markup in a WebView that's never shown, once it's loaded (its fonts too). Main thread. */
    private suspend fun loadPage(markup: String): WebView = suspendCancellableCoroutine { cont ->
        val web = WebView(AsukaModules.activity ?: app)
        web.settings.javaScriptEnabled = true
        var done = false
        web.webViewClient = object : WebViewClient() {
            override fun onPageFinished(view: WebView, url: String?) {
                if (done) return
                done = true
                view.evaluateJavascript("document.fonts ? document.fonts.ready.then(() => true) : true") { if (cont.isActive) cont.resume(view) }
            }
        }
        cont.invokeOnCancellation { Handler(Looper.getMainLooper()).post { web.destroy() } }
        web.loadDataWithBaseURL(null, markup, "text/html", "utf-8", null)
    }

    /**
     * Shows the print screen for adapter and returns once it's closed, with
     * what became of the job: printed (queued, started, or saved as a PDF),
     * cancelled, or failed.
     */
    private suspend fun print(name: String, adapter: PrintDocumentAdapter, job: Job, photo: Boolean = false): Outcome = withContext(Dispatchers.Main) {
        val act = AsukaModules.activity ?: throw AsukaUnsupportedException("print: no activity shows the app")
        val pm = act.getSystemService(Context.PRINT_SERVICE) as? PrintManager ?: throw PrintException(PrintError.UNAVAILABLE)
        val attrs = PrintAttributes.Builder()
        if (job.landscape) {
            // A size to turn: the region's paper, which the user can change.
            val size = if (job.letter) PrintAttributes.MediaSize.NA_LETTER else PrintAttributes.MediaSize.ISO_A4
            attrs.setMediaSize(size.asLandscape())
        }
        if (photo) attrs.setColorMode(PrintAttributes.COLOR_MODE_COLOR)
        suspendCancellableCoroutine { cont ->
            var printJob: PrintJob? = null
            val main = Handler(Looper.getMainLooper())
            val tracked = object : PrintDocumentAdapter() {
                override fun onStart() = adapter.onStart()
                override fun onLayout(old: PrintAttributes?, new: PrintAttributes, cancel: CancellationSignal?, cb: LayoutResultCallback, extras: Bundle?) =
                    adapter.onLayout(old, new, cancel, cb, extras)
                override fun onWrite(pages: Array<out PageRange>, dest: ParcelFileDescriptor, cancel: CancellationSignal?, cb: WriteResultCallback) =
                    adapter.onWrite(pages, dest, cancel, cb)
                override fun onFinish() {
                    adapter.onFinish()
                    // The spooler moves the job out of "created" as its screen closes.
                    fun check() {
                        when (printJob?.info?.state ?: PrintJobInfo.STATE_CANCELED) {
                            PrintJobInfo.STATE_CREATED -> main.postDelayed(::check, 50)
                            PrintJobInfo.STATE_CANCELED -> if (cont.isActive) cont.resume(Outcome.CANCELLED)
                            PrintJobInfo.STATE_FAILED -> if (cont.isActive) cont.resumeWithException(Exception("print: the job failed"))
                            else -> if (cont.isActive) cont.resume(Outcome.PRINTED)
                        }
                    }
                    main.post(::check)
                }
            }
            try {
                printJob = pm.print(name, tracked, attrs.build())
            } catch (e: Exception) {
                cont.resumeWithException(e)
            }
        }
    }
}

fun makePrinter(): Printer = AndroidPrinter(AsukaModules.context)

/** A PDF file, printed as it is. */
private class AsukaPdfFileAdapter(private val file: File, private val name: String) : PrintDocumentAdapter() {
    private val pages = runCatching {
        ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY).use { fd -> PdfRenderer(fd).use { it.pageCount } }
    }.getOrDefault(PrintDocumentInfo.PAGE_COUNT_UNKNOWN)

    override fun onLayout(old: PrintAttributes?, new: PrintAttributes, cancel: CancellationSignal?, cb: LayoutResultCallback, extras: Bundle?) {
        if (cancel?.isCanceled == true) return cb.onLayoutCancelled()
        cb.onLayoutFinished(PrintDocumentInfo.Builder(name).setContentType(PrintDocumentInfo.CONTENT_TYPE_DOCUMENT).setPageCount(pages).build(), old == null)
    }

    override fun onWrite(pages: Array<out PageRange>, dest: ParcelFileDescriptor, cancel: CancellationSignal?, cb: WriteResultCallback) {
        try {
            file.inputStream().use { i -> FileOutputStream(dest.fileDescriptor).use { o -> i.copyTo(o) } }
            if (cancel?.isCanceled == true) cb.onWriteCancelled() else cb.onWriteFinished(arrayOf(PageRange.ALL_PAGES))
        } catch (e: Exception) {
            cb.onWriteFailed(e.message)
        }
    }
}

/** An image, fit to the page's printable area and centred, as PrintHelper prints one. */
private class AsukaImageAdapter(private val ctx: Context, private val bitmap: Bitmap, private val name: String) : PrintDocumentAdapter() {
    private var attrs: PrintAttributes? = null

    override fun onLayout(old: PrintAttributes?, new: PrintAttributes, cancel: CancellationSignal?, cb: LayoutResultCallback, extras: Bundle?) {
        attrs = new
        if (cancel?.isCanceled == true) return cb.onLayoutCancelled()
        cb.onLayoutFinished(PrintDocumentInfo.Builder(name).setContentType(PrintDocumentInfo.CONTENT_TYPE_PHOTO).setPageCount(1).build(),
            old == null || old.mediaSize != new.mediaSize || old.minMargins != new.minMargins)
    }

    override fun onWrite(pages: Array<out PageRange>, dest: ParcelFileDescriptor, cancel: CancellationSignal?, cb: WriteResultCallback) {
        val doc = PrintedPdfDocument(ctx, attrs ?: return cb.onWriteFailed("no layout"))
        try {
            val page = doc.startPage(0)
            val r = page.info.contentRect
            val scale = minOf(r.width().toFloat() / bitmap.width, r.height().toFloat() / bitmap.height)
            val m = Matrix().apply {
                postScale(scale, scale)
                postTranslate(r.left + (r.width() - bitmap.width * scale) / 2, r.top + (r.height() - bitmap.height * scale) / 2)
            }
            page.canvas.drawBitmap(bitmap, m, Paint(Paint.FILTER_BITMAP_FLAG))
            doc.finishPage(page)
            if (cancel?.isCanceled == true) return cb.onWriteCancelled()
            FileOutputStream(dest.fileDescriptor).use { doc.writeTo(it) }
            cb.onWriteFinished(arrayOf(PageRange.ALL_PAGES))
        } catch (e: Exception) {
            cb.onWriteFailed(e.message)
        } finally {
            doc.close()
        }
    }
}

/**
 * The image at f, upright (its EXIF orientation applied), at most 3500
 * pixels on a side: PrintHelper's size, about 300 dpi over a page.
 */
private fun printBitmap(f: File): Bitmap? {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeFile(f.path, bounds)
    if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
    var sample = 1
    while (maxOf(bounds.outWidth, bounds.outHeight) / (sample * 2) >= 3500) sample *= 2
    val bmp = BitmapFactory.decodeFile(f.path, BitmapFactory.Options().apply { inSampleSize = sample }) ?: return null
    val turn = runCatching { ExifInterface(f.path).rotationDegrees }.getOrDefault(0)
    if (turn == 0) return bmp
    return Bitmap.createBitmap(bmp, 0, 0, bmp.width, bmp.height, Matrix().apply { postRotate(turn.toFloat()) }, true)
}
