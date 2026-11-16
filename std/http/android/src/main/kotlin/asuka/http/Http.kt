// asuka/http on Android: HttpURLConnection, the platform's own client,
// on the IO dispatcher; cancelling the call disconnects it.
package asuka.http

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
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

// LocalServer: a ServerSocket on the loopback, accepting on the IO
// dispatcher a fifth of a second at a time, so a cancelled next stops
// waiting (and the server stays for the next).
class LoopbackServers : LocalServer {
    private class Server(val socket: java.net.ServerSocket, val path: String, val page: String)

    private val servers = java.util.concurrent.ConcurrentHashMap<Long, Server>()

    override suspend fun listen(path: String, page: String): Long = withContext(Dispatchers.IO) {
        val s = java.net.ServerSocket(0, 4, java.net.InetAddress.getLoopbackAddress())
        s.soTimeout = 200
        val port = s.localPort.toLong()
        servers[port] = Server(s, path, page)
        port
    }

    override suspend fun next(port: Long): String = withContext(Dispatchers.IO) {
        val s = servers[port] ?: throw java.io.IOException("no loopback server at port $port")
        while (true) {
            val c = try {
                s.socket.accept()
            } catch (e: java.net.SocketTimeoutException) {
                ensureActive()  // the scope's: cancelled, it throws
                continue
            }
            try {
                val head = StringBuilder()
                val input = c.getInputStream()
                val buf = ByteArray(8192)
                while (head.length < 8192 && !head.contains("\r\n\r\n")) {
                    val n = input.read(buf)
                    if (n < 0) break
                    head.append(String(buf, 0, n, Charsets.ISO_8859_1))
                }
                val target = head.lineSequence().firstOrNull()?.split(" ")?.getOrNull(1) ?: ""
                val ours = target == s.path || target.startsWith(s.path + "?")
                val body = if (ours) s.page.toByteArray() else ByteArray(0)
                val status = if (ours) "200 OK" else "404 Not Found"
                val out = c.getOutputStream()
                out.write("HTTP/1.1 $status\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ${body.size}\r\nConnection: close\r\n\r\n".toByteArray())
                out.write(body)
                out.flush()
                if (ours) return@withContext "http://127.0.0.1:$port$target"
            } finally {
                c.close()
            }
        }
        @Suppress("UNREACHABLE_CODE")
        throw IllegalStateException()
    }

    override fun close(port: Long) {
        servers.remove(port)?.socket?.close()
    }
}

private val loopbackServers = LoopbackServers()

fun makeLocalServer(): LocalServer = loopbackServers
