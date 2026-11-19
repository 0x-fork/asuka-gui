// std/websocket's TLS settings (TLS.kt) on this computer's JVM:
// internal/testserver's echo over wss: with a test authority's
// certificate, through OkHttp.
package std.websocket

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.IOException
import kotlin.time.Duration.Companion.seconds

class TLSTest {
    private val url = "wss" + TestServer.tlsUrl.removePrefix("https") + "/ws"

    private suspend fun echo(c: Client): String {
        val s = c.connect(url, emptyMap(), emptyList(), 10.seconds)
        s.sendText("hello")
        val f = s.receive()
        s.close()
        return f.text
    }

    @Test fun trustedWithTheAuthority() = runBlocking<kotlin.Unit> {
        val c = OkHttpWebSockets().secured(TestServer.pem("ca.pem"), false, "", "", false)
        assertEquals("hello", echo(c))
        val m = OkHttpWebSockets().secured(TestServer.pem("ca.pem"), false, TestServer.pem("client.pem"), TestServer.pem("client.key"), false)
        assertEquals("hello", echo(m))
        assertEquals("hello", echo(OkHttpWebSockets().secured("", false, "", "", true)))
    }

    @Test fun refusedOtherwise() = runBlocking<kotlin.Unit> {
        try {
            echo(OkHttpWebSockets())
            fail("trusted by the system")
        } catch (e: IOException) {
        }
        try {
            echo(OkHttpWebSockets().secured(TestServer.pem("other-ca.pem"), false, "", "", false))
            fail("trusted by another authority")
        } catch (e: IOException) {
            assertTrue(e.message, e.message!!.contains("127.0.0.1's certificate isn't trusted"))
        }
    }
}
