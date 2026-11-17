// std/websocket on Android: OkHttp's WebSocket (HttpURLConnection has
// none), its listener's callbacks into a channel that receive reads. One
// OkHttpClient for the app's sockets: its threads and connection pool.
package std.websocket

import dev.asuka.AsukaReleasing
import dev.asuka.AsukaUnsupportedException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.withTimeoutOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString
import okio.ByteString.Companion.toByteString
import java.io.EOFException
import java.io.IOException
import java.net.SocketTimeoutException
import java.util.concurrent.TimeUnit
import kotlin.time.Duration

class OkHttpWebSockets : Client {
    override suspend fun connect(url: String, headers: Map<String, String>, protocols: List<String>, timeout: Duration): Socket {
        val client = if (timeout.isPositive()) {
            // newBuilder shares the client's threads and pool.
            shared.newBuilder().connectTimeout(timeout.inWholeMilliseconds.coerceAtLeast(1L), TimeUnit.MILLISECONDS).build()
        } else {
            shared
        }
        val req = Request.Builder().url(url)
        for ((k, v) in headers) req.header(k, v)
        if (protocols.isNotEmpty()) req.header("Sec-WebSocket-Protocol", protocols.joinToString(", "))
        val s = OkHttpSocket()
        val ws = client.newWebSocket(req.build(), s.listener)
        s.ws = ws
        try {
            if (!timeout.isPositive()) {
                s.opened.await()
            } else if (withTimeoutOrNull(timeout) { s.opened.await() } == null) {
                throw SocketTimeoutException("websocket: no answer from $url within $timeout")
            }
        } catch (e: Throwable) {
            ws.cancel()
            throw e
        }
        return s
    }

    companion object {
        // A WebSocket waits for messages as long as it's open: no read
        // timeout (OkHttp's is for HTTP's answers).
        val shared: OkHttpClient by lazy { OkHttpClient.Builder().readTimeout(0, TimeUnit.MILLISECONDS).build() }
    }
}

// A connection: what OkHttp's listener hears, queued for receive. Its
// end (a close, the connection dropped, a failure) is kept, what every
// receive answers once the messages before it are read.
class OkHttpSocket : Socket, AsukaReleasing {
    @Volatile var ws: WebSocket? = null
    val opened = CompletableDeferred<kotlin.Unit>()
    @Volatile private var chosen = ""
    private val frames = Channel<Frame>(Channel.UNLIMITED)
    private val lock = Any()
    @Volatile private var end: Frame? = null
    @Volatile private var failure: Throwable? = null

    val listener = object : WebSocketListener() {
        override fun onOpen(webSocket: WebSocket, response: Response) {
            chosen = response.header("Sec-WebSocket-Protocol") ?: ""
            opened.complete(kotlin.Unit)
        }

        override fun onMessage(webSocket: WebSocket, text: String) {
            frames.trySend(Frame(kind = FrameKind.TEXT, text = text))
        }

        override fun onMessage(webSocket: WebSocket, bytes: ByteString) {
            frames.trySend(Frame(kind = FrameKind.BINARY, data = bytes.toByteArray()))
        }

        // The server's close: answered with its code (OkHttp sends no
        // answer of its own), the socket's end.
        override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
            try {
                webSocket.close(code, null)
            } catch (e: IllegalArgumentException) {
                webSocket.close(1000, null)  // a code no close may send (1005: none given)
            }
            finish(Frame(kind = FrameKind.CLOSED, text = reason, code = code.toLong()), null)
        }

        override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
            finish(Frame(kind = FrameKind.CLOSED, text = reason, code = code.toLong()), null)
        }

        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
            if (!opened.isCompleted) {
                val e = if (response != null && response.code != 101) {
                    IOException("websocket: the server answered ${response.code} ${response.message}".trimEnd(), t)
                } else t
                opened.completeExceptionally(e)
                finish(null, e)
                return
            }
            // Ended without a close: 1006. Anything else is the network's
            // error, which receive throws.
            if (t is EOFException) finish(Frame(kind = FrameKind.CLOSED, code = 1006L), null) else finish(null, t)
        }
    }

    private fun finish(frame: Frame?, error: Throwable?) {
        synchronized(lock) {
            if (end != null || failure != null) return
            end = frame
            failure = error
        }
        frames.close()
    }

    override fun protocol(): String = chosen

    override suspend fun receive(): Frame {
        frames.receiveCatching().getOrNull()?.let { return it }
        failure?.let { throw it }
        return end ?: Frame(kind = FrameKind.CLOSED, code = 1006L)
    }

    override suspend fun sendText(text: String) {
        if (ws?.send(text) != true) throw IOException("websocket: the connection is closed")
    }

    override suspend fun sendBinary(data: ByteArray) {
        if (ws?.send(data.toByteString()) != true) throw IOException("websocket: the connection is closed")
    }

    override fun shutdown(code: Long, reason: String) {
        ws?.close(code.toInt(), reason.ifEmpty { null })
    }

    // Let go: a normal close if it's open (OkHttp gives the server a
    // minute to answer, then drops the connection); one closing already
    // finishes so.
    override fun close() {
        val w = ws ?: return
        if (end != null || failure != null) w.cancel() else w.close(1000, null)
    }

    override fun released() = close()
}

fun makeClient(): Client = OkHttpWebSockets()

// A Socket comes from Client.connect: Asuka doesn't make one.
fun makeSocket(): Socket = throw AsukaUnsupportedException("websocket: a Socket comes from Client.connect")
