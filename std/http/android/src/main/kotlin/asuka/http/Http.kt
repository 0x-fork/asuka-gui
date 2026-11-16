// asuka/http on Android: HttpURLConnection, the platform's own client,
// on the IO dispatcher; cancelling the call disconnects it.
package asuka.http

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlin.time.Duration

class ConnectionTransport : Transport {
    override suspend fun download(method: String, url: String, headers: Map<String, String>, body: ByteArray, timeout: Duration, path: String, progress: (Long, Long) -> kotlin.Unit): Response =
        fetchToFile(method, url, headers, body, timeout, path, progress)

    override suspend fun send(method: String, url: String, headers: Map<String, String>, body: ByteArray, timeout: Duration): Response =
        withContext(Dispatchers.IO) {
            val c = URL(url).openConnection() as HttpURLConnection
            suspendCancellableCoroutine { k ->
                k.invokeOnCancellation { c.disconnect() }
                try {
                    c.requestMethod = method
                    c.instanceFollowRedirects = true
                    if (timeout.isPositive()) {
                        val ms = timeout.inWholeMilliseconds.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
                        c.connectTimeout = ms
                        c.readTimeout = ms
                    }
                    for ((k2, v) in headers) c.setRequestProperty(k2, v)
                    if (body.isNotEmpty()) {
                        c.doOutput = true
                        c.outputStream.use { it.write(body) }
                    }
                    val status = c.responseCode
                    val stream = if (status >= 400) c.errorStream else c.inputStream
                    val data = stream?.use { it.readBytes() } ?: ByteArray(0)
                    val out = HashMap<String, String>()
                    for ((k2, v) in c.headerFields) if (k2 != null) out[k2.lowercase()] = v.joinToString(", ")
                    k.resume(Response(status = status.toLong(), url = c.url.toString(), headers = out, body = data))
                } catch (e: Throwable) {
                    k.resumeWithException(e)
                } finally {
                    c.disconnect()
                }
            }
        }
}

// download, on the IO dispatcher: the body read in chunks into a file
// beside path, renamed once it's all in; cancelling disconnects it and
// leaves no file.
suspend fun ConnectionTransport.fetchToFile(method: String, url: String, headers: Map<String, String>, body: ByteArray, timeout: Duration, path: String, progress: (Long, Long) -> kotlin.Unit): Response =
    withContext(Dispatchers.IO) {
        val c = URL(url).openConnection() as HttpURLConnection
        val to = File(path)
        val part = File(path + ".download")
        suspendCancellableCoroutine { k ->
            k.invokeOnCancellation { c.disconnect() }
            try {
                c.requestMethod = method
                c.instanceFollowRedirects = true
                if (timeout.isPositive()) {
                    val ms = timeout.inWholeMilliseconds.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
                    c.connectTimeout = ms
                    c.readTimeout = ms  // the longest wait for more
                }
                for ((k2, v) in headers) c.setRequestProperty(k2, v)
                if (body.isNotEmpty()) {
                    c.doOutput = true
                    c.outputStream.use { it.write(body) }
                }
                val status = c.responseCode
                val total = c.contentLengthLong
                to.absoluteFile.parentFile?.mkdirs()
                var got = 0L
                var reported = 0L
                val stream = if (status >= 400) c.errorStream else c.inputStream
                FileOutputStream(part).use { out ->
                    stream?.use { input ->
                        val buf = ByteArray(64 * 1024)
                        while (true) {
                            val n = input.read(buf)
                            if (n < 0) break
                            out.write(buf, 0, n)
                            got += n
                            val now = System.nanoTime()
                            if (now - reported > 100_000_000L) {
                                reported = now
                                progress(got, total)
                            }
                        }
                    }
                }
                if (!part.renameTo(to)) {
                    to.delete()
                    if (!part.renameTo(to)) throw java.io.IOException("can't write $path")
                }
                progress(got, if (total >= 0) total else got)
                val out = HashMap<String, String>()
                for ((k2, v) in c.headerFields) if (k2 != null) out[k2.lowercase()] = v.joinToString(", ")
                k.resume(Response(status = status.toLong(), url = c.url.toString(), headers = out, body = ByteArray(0)))
            } catch (e: Throwable) {
                part.delete()
                k.resumeWithException(e)
            } finally {
                c.disconnect()
            }
        }
    }

fun makeTransport(): Transport = ConnectionTransport()
