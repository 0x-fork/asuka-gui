// Client and Socket on this computer's JVM (OkHttp runs there too),
// against internal/testserver's /ws: its URL the asuka.testserver
// property, or started here (go run).
package std.websocket

import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.fail
import org.junit.Test
import java.io.File
import java.io.IOException
import java.net.ServerSocket
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds

class SocketTest {
    private val ws = TestServer.url.replaceFirst("http://", "ws://") + "/ws"

    private suspend fun connect(headers: Map<String, String> = emptyMap(), protocols: List<String> = emptyList()) =
        OkHttpWebSockets().connect(ws, headers, protocols, 10.seconds)

    // A receive that doesn't come fails the test rather than hanging it.
    private suspend fun Socket.next(): Frame = withTimeout(10.seconds) { receive() }

    @Test fun echoesTextAndBinary() = runBlocking {
        val s = connect()
        assertEquals("", s.protocol())
        s.sendText("hello")
        var f = s.next()
        assertEquals(FrameKind.TEXT, f.kind)
        assertEquals("hello", f.text)
        s.sendBinary(byteArrayOf(0, 1, 2, -1))
        f = s.next()
        assertEquals(FrameKind.BINARY, f.kind)
        assertArrayEquals(byteArrayOf(0, 1, 2, -1), f.data)
        val big = ByteArray(200_000) { it.toByte() }  // a 64-bit length
        s.sendBinary(big)
        assertArrayEquals(big, s.next().data)
        s.close()
    }

    @Test fun sendsHeaders() = runBlocking {
        val s = connect(headers = mapOf("X-Token" to "abc"))
        val f = s.next()
        assertEquals(FrameKind.TEXT, f.kind)
        assertEquals("header:abc", f.text)
        s.close()
    }

    @Test fun choosesAProtocol() = runBlocking {
        val s = connect(protocols = listOf("superchat", "chat"))
        assertEquals("chat", s.protocol())
        s.close()
    }

    // The server's close: its code and reason, then always that.
    @Test fun theServerCloses() = runBlocking {
        val s = connect()
        s.sendText("close please")
        for (i in 0 until 2) {
            val f = s.next()
            assertEquals(FrameKind.CLOSED, f.kind)
            assertEquals(4000L, f.code)
            assertEquals("bye", f.text)
        }
        try {
            s.sendText("more")
            fail("sent on a closed socket")
        } catch (e: IOException) {
        }
        s.close()
    }

    @Test fun theServerClosesNormally() = runBlocking {
        val s = connect()
        s.sendText("close normally")
        val f = s.next()
        assertEquals(FrameKind.CLOSED, f.kind)
        assertEquals(1000L, f.code)
        assertEquals("", f.text)
        s.close()
    }

    // shutdown: the server answers with the code, and that's the end.
    @Test fun theClientShutsDown() = runBlocking {
        val s = connect()
        s.sendText("before")
        assertEquals("before", s.next().text)
        s.shutdown(4001, "done")
        val f = s.next()
        assertEquals(FrameKind.CLOSED, f.kind)
        assertEquals(4001L, f.code)
        assertEquals("done", f.text)
        assertEquals(4001L, s.next().code)
        s.close()
    }

    @Test fun aRefusedUpgradeThrows() = runBlocking {
        try {
            OkHttpWebSockets().connect(TestServer.url.replaceFirst("http://", "ws://") + "/status/404", emptyMap(), emptyList(), 10.seconds)
            fail("connected to a 404")
        } catch (e: IOException) {
            assert(e.message!!.contains("404")) { e.message!! }
        }
    }

    // A server that accepts the connection and never answers: connect
    // gives up after its timeout.
    @Test fun connectTimesOut() = runBlocking {
        ServerSocket(0).use { server ->
            try {
                OkHttpWebSockets().connect("ws://127.0.0.1:${server.localPort}/", emptyMap(), emptyList(), 300.milliseconds)
                fail("connected to a server that never answered")
            } catch (e: IOException) {
                // java.net.SocketTimeoutException
            }
        }
    }

    // The connection dropped without a close: 1006.
    @Test fun aDroppedConnectionIs1006() = runBlocking {
        ServerSocket(0).use { server ->
            val t = Thread {
                server.accept().use { c ->
                    val head = StringBuilder()
                    val input = c.getInputStream()
                    while (!head.endsWith("\r\n\r\n")) head.append(input.read().toChar())
                    val key = Regex("(?i)sec-websocket-key: *(\\S+)").find(head)!!.groupValues[1]
                    val accept = java.util.Base64.getEncoder().encodeToString(
                        java.security.MessageDigest.getInstance("SHA-1").digest((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").toByteArray()))
                    c.getOutputStream().write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: $accept\r\n\r\n".toByteArray())
                    c.getOutputStream().write(byteArrayOf(0x81.toByte(), 2, 'h'.code.toByte(), 'i'.code.toByte()))
                    c.getOutputStream().flush()
                }  // closed: no close frame
            }
            t.start()
            val s = OkHttpWebSockets().connect("ws://127.0.0.1:${server.localPort}/", emptyMap(), emptyList(), 10.seconds)
            assertEquals("hi", s.next().text)
            val f = s.next()
            assertEquals(FrameKind.CLOSED, f.kind)
            assertEquals(1006L, f.code)
            t.join()
            s.close()
        }
    }

    @Test fun makingASocketIsUnsupported() {
        try {
            makeSocket()
            fail("made a Socket")
        } catch (e: dev.asuka.AsukaUnsupportedException) {
        }
    }
}

// internal/testserver: the URL the CLI's test gives (asuka.testserver),
// else go run in the repository this module is in, until the JVM ends.
object TestServer {
    val url: String by lazy {
        System.getProperty("asuka.testserver")?.takeIf { it.isNotEmpty() } ?: start()
    }

    private fun start(): String {
        var dir: File? = File(System.getProperty("user.dir")).absoluteFile
        while (dir != null && !File(dir, "internal/testserver/cmd").isDirectory) dir = dir.parentFile
        if (dir == null) error("no Asuka repository above ${System.getProperty("user.dir")}: set asuka.testserver")
        val p = ProcessBuilder("go", "run", "./internal/testserver/cmd").directory(dir)
            .redirectError(ProcessBuilder.Redirect.INHERIT).start()
        Runtime.getRuntime().addShutdownHook(Thread { p.outputStream.close(); p.destroy() })
        return p.inputStream.bufferedReader().readLine() ?: error("the test server didn't start")
    }
}
