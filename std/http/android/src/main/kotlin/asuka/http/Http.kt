// asuka/http on Android: HttpURLConnection, the platform's own client,
// on the IO dispatcher; cancelling the call disconnects it.
package asuka.http

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import java.net.HttpURLConnection
import java.net.URL
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlin.time.Duration

class ConnectionTransport : Transport {
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

fun makeTransport(): Transport = ConnectionTransport()
