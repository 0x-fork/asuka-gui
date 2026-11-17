// Transport.open and its Body on this computer's JVM (HttpURLConnection
// is the JDK's here), against internal/testserver: its URL the
// asuka.testserver property, or started here (go run).
package std.http

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.File
import java.io.IOException
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds

class BodyTest {
    private val t = ConnectionTransport()

    private suspend fun open(path: String, timeout: Duration = 10.seconds, method: String = "GET", body: ByteArray = ByteArray(0), headers: Map<String, String> = emptyMap()) =
        t.open(method, TestServer.url + path, headers, body, timeout)

    // Each piece as it was sent: the first before the rest was.
    @Test fun piecesArriveBeforeTheEnd() = runBlocking {
        val b = open("/stream")
        assertEquals(200L, b.head().status)
        assertEquals("text/plain; charset=utf-8", b.head().headers["content-type"])
        assertEquals(0, b.head().body.size)
        val pieces = ArrayList<String>()
        while (true) {
            val p = b.read()
            if (p.isEmpty()) break
            pieces.add(String(p))
        }
        assertEquals("one\n", pieces[0])
        assertTrue("pieces: $pieces", pieces.size >= 3)
        assertEquals("one\ntwo\nthree\n", pieces.joinToString(""))
        assertEquals(0, b.read().size)  // and at the end, empty again
        b.close()
    }

    @Test fun anErrorStatusOpens() = runBlocking {
        val b = open("/status/404")
        assertEquals(404L, b.head().status)
        assertEquals("missing", readAll(b))
    }

    @Test fun sendsMethodHeadersAndBody() = runBlocking {
        val b = open("/echo", method = "POST", body = "hi".toByteArray(), headers = mapOf("X-Token" to "abc"))
        assertTrue(b.head().url.endsWith("/echo"))
        val s = readAll(b)
        assertTrue(s, s.contains("\"method\":\"POST\""))
        assertTrue(s, s.contains("\"body\":\"hi\""))
        assertTrue(s, s.contains("\"x-token\":\"abc\""))
    }

    // No piece within the timeout: the read throws.
    @Test fun aStalledBodyTimesOut() = runBlocking {
        val b = open("/stall", timeout = 300.milliseconds)
        assertEquals(200L, b.head().status)
        try {
            b.read()
            fail("read a stalled body")
        } catch (e: IOException) {
            // java.net.SocketTimeoutException
        }
        b.close()
    }

    // A cancelled read disconnects: it ends, and the body reads as ended.
    @Test fun cancellingAReadDisconnects() = runBlocking {
        val b = open("/stall", timeout = Duration.ZERO)
        val r = async(Dispatchers.IO) { b.read() }
        Thread.sleep(100)  // the read waiting in the socket (the test passes either way)
        r.cancel()
        withTimeout(10.seconds) { r.join() }
        assertTrue(r.isCancelled)
        assertEquals(0, b.read().size)
    }

    // Closed while a read waits: the read ends as the body's end.
    @Test fun closingEndsAWaitingRead() = runBlocking {
        val b = open("/stall", timeout = Duration.ZERO)
        val r = async(Dispatchers.IO) { b.read() }
        Thread.sleep(100)  // the read waiting in the socket (the test passes either way)
        b.close()
        assertEquals(0, withTimeout(10.seconds) { r.await() }.size)
    }

    @Test fun aRefusedConnectionThrows() = runBlocking {
        val port = java.net.ServerSocket(0).use { it.localPort }  // closed: nothing listens
        try {
            t.open("GET", "http://127.0.0.1:$port/", emptyMap(), ByteArray(0), 5.seconds)
            fail("opened a refused connection")
        } catch (e: IOException) {
        }
    }

    @Test fun makingABodyIsUnsupported() {
        try {
            makeBody()
            fail("made a Body")
        } catch (e: dev.asuka.AsukaUnsupportedException) {
            assertFalse(e.message.isNullOrEmpty())
        }
    }

    private suspend fun readAll(b: Body): String {
        val out = java.io.ByteArrayOutputStream()
        while (true) {
            val p = b.read()
            if (p.isEmpty()) return out.toString(Charsets.UTF_8)
            out.write(p)
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
