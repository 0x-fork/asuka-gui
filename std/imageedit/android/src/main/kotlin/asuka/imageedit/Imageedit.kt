// asuka/imageedit on Android: BitmapFactory decodes (JPEG, PNG, WebP,
// HEIC) a power of two smaller where the first resize allows, then turns
// it upright as its EXIF says; Matrix does the actions; Bitmap.compress
// writes JPEG or PNG, in the app's cache (asuka-images);
// MediaMetadataRetriever takes a video's frame. Off the main thread.
package asuka.imageedit

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Matrix
import android.graphics.Paint
import android.media.MediaMetadataRetriever
import androidx.exifinterface.media.ExifInterface
import dev.asuka.AsukaModules
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.UUID
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.time.Duration

private object Edit {
    private val app get() = AsukaModules.context

    // Sources: as ui.Image's. The file, and whether it's a temporary copy.

    private fun local(source: String): Pair<File, Boolean> = when {
        source.startsWith("http://") || source.startsWith("https://") -> download(source) to true
        source.startsWith("file://") -> File(android.net.Uri.parse(source).path ?: throw EditException(EditError.NO_IMAGE)) to false
        source.startsWith("/") -> File(source) to false
        // An asset: pushed by asuka dev, else in the APK.
        File(app.filesDir, "asuka/assets/$source").exists() -> File(app.filesDir, "asuka/assets/$source") to false
        else -> {
            val f = temp(source.substringAfterLast('.', "img"))
            try {
                app.assets.open(source).use { i -> f.outputStream().use { o -> i.copyTo(o) } }
            } catch (_: java.io.IOException) {
                f.delete(); throw EditException(EditError.NO_IMAGE)
            }
            f to true
        }
    }.also { (f, _) -> if (!f.isFile) throw EditException(EditError.NO_IMAGE) }

    private fun temp(ext: String) = File(File(app.cacheDir, "asuka-images-in").apply { mkdirs() }, UUID.randomUUID().toString() + "." + ext)

    private fun download(url: String): File {
        val c = URL(url).openConnection() as HttpURLConnection
        try {
            if (c.responseCode !in 200..299) throw Exception("HTTP ${c.responseCode}")
            val f = temp("img")
            c.inputStream.use { i -> f.outputStream().use { o -> i.copyTo(o) } }
            return f
        } finally {
            c.disconnect()
        }
    }

    private fun bounds(f: File): Pair<Int, Int> {
        val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(f.path, o)
        if (o.outWidth <= 0 || o.outHeight <= 0) throw EditException(EditError.NO_IMAGE)
        return o.outWidth to o.outHeight
    }

    private fun orientation(f: File): Int =
        runCatching { ExifInterface(f.path).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL) }
            .getOrDefault(ExifInterface.ORIENTATION_NORMAL)

    private fun quarterTurned(o: Int) = o == ExifInterface.ORIENTATION_TRANSPOSE || o == ExifInterface.ORIENTATION_ROTATE_90 ||
        o == ExifInterface.ORIENTATION_TRANSVERSE || o == ExifInterface.ORIENTATION_ROTATE_270

    /** Decodes upright, a power of two smaller where a first resize allows (memory). */
    private fun decode(f: File, first: Step?): Bitmap {
        val (w, h) = bounds(f)
        val o = orientation(f)
        val (uw, uh) = if (quarterTurned(o)) h to w else w to h
        var sample = 1
        if (first?.op == Op.RESIZE) {
            val (tw, th) = target(first, uw, uh)
            while (uw / (sample * 2) >= tw && uh / (sample * 2) >= th) sample *= 2
        }
        val bmp = BitmapFactory.decodeFile(f.path, BitmapFactory.Options().apply { inSampleSize = sample; inPreferredConfig = Bitmap.Config.ARGB_8888 })
            ?: throw EditException(EditError.NO_IMAGE)
        val m = Matrix()
        when (o) {
            ExifInterface.ORIENTATION_FLIP_HORIZONTAL -> m.setScale(-1f, 1f)
            ExifInterface.ORIENTATION_ROTATE_180 -> m.setRotate(180f)
            ExifInterface.ORIENTATION_FLIP_VERTICAL -> m.setScale(1f, -1f)
            ExifInterface.ORIENTATION_TRANSPOSE -> { m.setRotate(90f); m.postScale(-1f, 1f) }
            ExifInterface.ORIENTATION_ROTATE_90 -> m.setRotate(90f)
            ExifInterface.ORIENTATION_TRANSVERSE -> { m.setRotate(-90f); m.postScale(-1f, 1f) }
            ExifInterface.ORIENTATION_ROTATE_270 -> m.setRotate(-90f)
            else -> return bmp
        }
        return Bitmap.createBitmap(bmp, 0, 0, bmp.width, bmp.height, m, true).also { if (it !== bmp) bmp.recycle() }
    }

    /** A resize's size: a 0 keeps the aspect ratio from the other. */
    private fun target(a: Step, w: Int, h: Int): Pair<Int, Int> {
        var tw = a.width.toInt(); var th = a.height.toInt()
        if (tw <= 0 && th <= 0) return w to h
        if (tw <= 0) tw = maxOf(1, (w.toDouble() * th / h).roundToInt())
        if (th <= 0) th = maxOf(1, (h.toDouble() * tw / w).roundToInt())
        return tw to th
    }

    private fun apply(actions: List<Step>, image: Bitmap): Bitmap {
        var img = image
        fun replace(next: Bitmap) { if (next !== img) img.recycle(); img = next }
        for (a in actions) {
            val w = img.width; val h = img.height
            when (a.op) {
                Op.RESIZE -> {
                    val (tw, th) = target(a, w, h)
                    if (tw != w || th != h) replace(Bitmap.createScaledBitmap(img, tw, th, true))
                }
                Op.CROP -> {
                    val x = a.x.toInt().coerceIn(0, w); val y = a.y.toInt().coerceIn(0, h)
                    val cw = minOf(a.width.toInt(), w - x); val ch = minOf(a.height.toInt(), h - y)
                    if (cw < 1 || ch < 1) throw Exception("the crop is outside the ${w}×$h image")
                    replace(Bitmap.createBitmap(img, x, y, cw, ch))
                }
                Op.ROTATE -> {
                    val deg = a.degrees % 360
                    if (deg == 0.0) continue
                    val t = Math.toRadians(deg)
                    val quarter = deg % 90 == 0.0
                    val nw = w * abs(cos(t)) + h * abs(sin(t)); val nh = w * abs(sin(t)) + h * abs(cos(t))
                    val ow = if (quarter) nw.roundToInt() else ceil(nw).toInt()
                    val oh = if (quarter) nh.roundToInt() else ceil(nh).toInt()
                    val out = Bitmap.createBitmap(ow, oh, Bitmap.Config.ARGB_8888)
                    val m = Matrix().apply {
                        postTranslate(-w / 2f, -h / 2f)
                        postRotate(deg.toFloat()) // clockwise: the canvas's y is down
                        postTranslate(ow / 2f, oh / 2f)
                    }
                    Canvas(out).drawBitmap(img, m, Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG))
                    replace(out)
                }
                Op.FILL, Op.PIXELATE -> {
                    if (!img.isMutable) replace(img.copy(Bitmap.Config.ARGB_8888, true))
                    val x = a.x.toInt().coerceIn(0, w); val y = a.y.toInt().coerceIn(0, h)
                    val x1 = (a.x + a.width).toInt().coerceIn(0, w); val y1 = (a.y + a.height).toInt().coerceIn(0, h)
                    if (x1 <= x || y1 <= y) continue
                    if (a.op == Op.FILL) {
                        val paint = Paint().apply { color = Color.BLACK or a.rgb.toInt(); isAntiAlias = false }
                        Canvas(img).drawRect(x.toFloat(), y.toFloat(), x1.toFloat(), y1.toFloat(), paint)
                    } else {
                        pixelate(img, x, y, x1, y1, maxOf(a.block.toInt(), 1))
                    }
                }
                Op.FLIP_HORIZONTAL, Op.FLIP_VERTICAL -> {
                    val m = Matrix().apply { if (a.op == Op.FLIP_HORIZONTAL) setScale(-1f, 1f) else setScale(1f, -1f) }
                    replace(Bitmap.createBitmap(img, 0, 0, w, h, m, true))
                }
                else -> throw Exception("unknown action ${a.op.value}")
            }
        }
        return img
    }

    /**
     * Squares of block pixels, aligned to the image, each the average of
     * all it covers; only those within the rectangle change.
     */
    private fun pixelate(img: Bitmap, x0: Int, y0: Int, x1: Int, y1: Int, block: Int) {
        val w = img.width; val h = img.height
        var by = y0 / block * block
        while (by < y1) {
            val by1 = minOf(by + block, h)
            var bx = x0 / block * block
            while (bx < x1) {
                val bx1 = minOf(bx + block, w)
                val bw = bx1 - bx; val bh = by1 - by
                val px = IntArray(bw * bh)
                img.getPixels(px, 0, bw, bx, by, bw, bh)
                var a = 0L; var r = 0L; var g = 0L; var b = 0L
                for (p in px) { a += p ushr 24; r += p shr 16 and 255; g += p shr 8 and 255; b += p and 255 }
                val n = px.size
                val v = Color.argb((a / n).toInt(), (r / n).toInt(), (g / n).toInt(), (b / n).toInt())
                val cx0 = maxOf(bx, x0); val cy0 = maxOf(by, y0)
                val cw = minOf(bx1, x1) - cx0; val ch = minOf(by1, y1) - cy0
                img.setPixels(IntArray(cw * ch) { v }, 0, cw, cx0, cy0, cw, ch)
                bx += block
            }
            by += block
        }
    }

    /** Writes to the caches: PNG, or JPEG on white (it has no transparency). */
    private fun write(image: Bitmap, png: Boolean, quality: Double): Result {
        var img = image
        if (!png && image.hasAlpha()) {
            img = Bitmap.createBitmap(image.width, image.height, Bitmap.Config.ARGB_8888)
            Canvas(img).apply { drawColor(Color.WHITE); drawBitmap(image, 0f, 0f, null) }
            image.recycle()
        }
        val dir = File(app.cacheDir, "asuka-images").apply { mkdirs() }
        val f = File(dir, UUID.randomUUID().toString() + if (png) ".png" else ".jpg")
        f.outputStream().use { o ->
            if (!img.compress(if (png) Bitmap.CompressFormat.PNG else Bitmap.CompressFormat.JPEG, (quality * 100).roundToInt().coerceIn(0, 100), o)) {
                throw Exception("can't write ${f.path}")
            }
        }
        val out = Result(path = f.absolutePath, width = img.width.toLong(), height = img.height.toLong())
        img.recycle()
        return out
    }

    /** The video's frame at the time (exactly), upright (the retriever turns it as the video's rotation says). */
    private fun frame(source: String, atMillis: Long): Bitmap {
        val m = MediaMetadataRetriever()
        try {
            try {
                when {
                    source.startsWith("http://") || source.startsWith("https://") -> m.setDataSource(source, HashMap())
                    source.startsWith("file://") -> m.setDataSource(android.net.Uri.parse(source).path)
                    source.startsWith("/") -> m.setDataSource(source)
                    File(app.filesDir, "asuka/assets/$source").exists() -> m.setDataSource(File(app.filesDir, "asuka/assets/$source").path)
                    else -> app.assets.openFd(source).use { fd -> m.setDataSource(fd.fileDescriptor, fd.startOffset, fd.length) }
                }
            } catch (_: Exception) {
                throw EditException(EditError.NO_IMAGE)
            }
            return m.getFrameAtTime(maxOf(atMillis, 0) * 1000, MediaMetadataRetriever.OPTION_CLOSEST)?.let {
                // A hardware bitmap can't be drawn on: a copy that can.
                if (it.config == Bitmap.Config.ARGB_8888) it else it.copy(Bitmap.Config.ARGB_8888, false).also { _ -> it.recycle() }
            } ?: throw EditException(EditError.NO_IMAGE)
        } finally {
            m.release()
        }
    }

    fun edit(source: String, steps: List<Step>, png: Boolean, quality: Double): Result {
        val (f, temp) = local(source)
        try {
            return write(apply(steps, decode(f, steps.firstOrNull())), png, quality)
        } finally {
            if (temp) f.delete()
        }
    }

    fun size(source: String): Result {
        val (f, temp) = local(source)
        try {
            val (w, h) = bounds(f)
            return if (quarterTurned(orientation(f))) Result(path = "", width = h.toLong(), height = w.toLong())
            else Result(path = "", width = w.toLong(), height = h.toLong())
        } finally {
            if (temp) f.delete()
        }
    }

    fun videoFrame(source: String, atMillis: Long, steps: List<Step>, png: Boolean, quality: Double): Result =
        write(apply(steps, frame(source, atMillis)), png, quality)
}

/** The work, off the main thread; running out of memory is an image too large. */
private suspend fun <T> work(f: () -> T): T = withContext(Dispatchers.IO) {
    try {
        f()
    } catch (e: OutOfMemoryError) {
        throw Exception("the image is too large to edit")
    }
}

private class BitmapEditor : Editor {
    override suspend fun edit(source: String, steps: List<Step>, format: Format, quality: Double): Result =
        work { Edit.edit(source, steps, format == Format.PNG, quality) }

    override suspend fun size(source: String): Result = work { Edit.size(source) }

    override suspend fun videoFrame(source: String, at: Duration, steps: List<Step>, format: Format, quality: Double): Result =
        work { Edit.videoFrame(source, at.inWholeMilliseconds, steps, format == Format.PNG, quality) }
}

fun makeEditor(): Editor = BitmapEditor()
