// std/http's TLS settings (TLS.kt) on this computer's JVM, against
// internal/testserver over HTTPS with a test authority's certificate
// (TestServer.tlsUrl, the authority's files TestServer.pki).
package std.http

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.File
import java.io.IOException
import kotlin.time.Duration.Companion.seconds

class TLSTest {
    private val plain = ConnectionTransport()
    private val ca = TestServer.pem("ca.pem")

    private suspend fun get(t: Transport, path: String = "/mtls"): String {
        val r = t.send("GET", TestServer.tlsUrl + path, emptyMap(), ByteArray(0), 10.seconds)
        return "${r.status} ${String(r.body).trim()}"
    }

    private suspend fun secured(ca: String = "", trustSystem: Boolean = false, certificate: String = "", key: String = "", insecure: Boolean = false) =
        plain.secured(TLS(ca = ca, trustSystem = trustSystem, certificate = certificate, key = key, insecureSkipVerify = insecure))

    private suspend fun refused(t: Transport): String {
        try {
            get(t)
        } catch (e: IOException) {
            return e.message ?: ""
        }
        fail("trusted")
        return ""
    }

    @Test fun theSystemDoesntTrustTheTestAuthority() = runBlocking<kotlin.Unit> {
        refused(plain)
    }

    @Test fun theAuthorityTrusted() = runBlocking<kotlin.Unit> {
        assertEquals("403 no client certificate", get(secured(ca = ca)))
        assertEquals("403 no client certificate", get(secured(ca = ca, trustSystem = true)))
        // The same settings, the same transport.
        assertSame(secured(ca = ca), secured(ca = ca))
    }

    @Test fun anotherAuthorityRefused() = runBlocking<kotlin.Unit> {
        val why = refused(secured(ca = TestServer.pem("other-ca.pem")))
        assertTrue(why, why.contains("127.0.0.1's certificate isn't trusted"))
    }

    @Test fun insecureChecksNothing() = runBlocking<kotlin.Unit> {
        assertEquals("403 no client certificate", get(secured(insecure = true)))
    }

    @Test fun clientCertificates() = runBlocking<kotlin.Unit> {
        assertEquals("200 client: Asuka Test Client", get(secured(ca = ca, certificate = TestServer.pem("client.pem"), key = TestServer.pem("client.key"))))
        assertEquals("200 client: Asuka Test Client", get(secured(ca = ca, certificate = TestServer.pem("client.pem"), key = TestServer.pem("client-sec1.key"))))
        assertEquals("200 client: Asuka Test RSA Client", get(secured(ca = ca, certificate = TestServer.pem("rsa-client.pem"), key = TestServer.pem("rsa-client.key"))))
    }

    @Test fun streamsAndDownloads() = runBlocking<kotlin.Unit> {
        val t = secured(ca = ca)
        val b = t.open("GET", TestServer.tlsUrl + "/stream", emptyMap(), ByteArray(0), 10.seconds)
        val all = StringBuilder()
        while (true) {
            val p = b.read()
            if (p.isEmpty()) break
            all.append(String(p))
        }
        assertEquals("200 one\ntwo\nthree\n", "${b.head().status} $all")
        val f = File.createTempFile("tls", ".json")
        f.delete()
        val r = t.download("GET", TestServer.tlsUrl + "/echo", emptyMap(), ByteArray(0), 10.seconds, f.path) { _, _ -> }
        assertEquals(200L, r.status)
        assertTrue(f.readText().contains("\"uri\":\"/echo\""))
        f.delete()
    }

    @Test fun whatCantBeRead() = runBlocking<kotlin.Unit> {
        try {
            secured(ca = "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n")
            fail("made")
        } catch (e: IllegalArgumentException) {
            assertTrue(e.message, e.message!!.startsWith("tls: ca: a certificate can't be read"))
        }
        try {
            secured(certificate = TestServer.pem("client.pem"), key = TestServer.pem("rsa-client.key"))
            fail("made")
        } catch (e: IllegalArgumentException) {
            assertEquals("tls: the key isn't the certificate's", e.message)
        }
    }
}
