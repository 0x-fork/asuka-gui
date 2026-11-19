// TLS.kt: std/http's TLS settings (http.TLS) on Android, as an
// SSLContext: the authorities to trust a KeyStore's, through a
// TrustManagerFactory's X509TrustManager (the system's checked first when
// it's trusted too), the client's certificate a KeyManagerFactory's from
// a KeyStore with its key. The same file is std/websocket's
// (std/websocket/android/.../TLS.kt, its package aside; std's tests keep
// them alike).
package std.websocket

import java.io.ByteArrayOutputStream
import java.net.Socket
import java.security.KeyFactory
import java.security.KeyStore
import java.security.PrivateKey
import java.security.Signature
import java.security.cert.CertificateException
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.security.spec.PKCS8EncodedKeySpec
import java.util.Base64
import javax.net.ssl.HostnameVerifier
import javax.net.ssl.HttpsURLConnection
import javax.net.ssl.KeyManagerFactory
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLEngine
import javax.net.ssl.SSLSocket
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509ExtendedTrustManager
import javax.net.ssl.X509TrustManager

/** A client's TLS settings, read once: the same settings, the same one. */
class TLSPolicy private constructor(
    val socketFactory: SSLSocketFactory,
    val trustManager: X509TrustManager,
    /** Every name's, when nothing's checked (insecure); else none: the platform's. */
    val hostnameVerifier: HostnameVerifier?,
) {
    /** Has a connection's TLS go as the policy says. */
    fun apply(c: java.net.URLConnection) {
        if (c is HttpsURLConnection) {
            c.sslSocketFactory = socketFactory
            hostnameVerifier?.let { c.hostnameVerifier = it }
        }
    }

    companion object {
        private val made = HashMap<List<Any>, TLSPolicy>()

        /** The policy for these settings (http.TLS's fields), made the first time they're asked for. It throws for what it can't read. */
        fun of(ca: String, trustSystem: Boolean, certificate: String, key: String, insecure: Boolean): TLSPolicy = synchronized(made) {
            made.getOrPut(listOf(ca, trustSystem, certificate, key, insecure)) { make(ca, trustSystem, certificate, key, insecure) }
        }

        private fun make(ca: String, trustSystem: Boolean, certificate: String, key: String, insecure: Boolean): TLSPolicy {
            val tm: X509ExtendedTrustManager = when {
                insecure -> Unchecked
                ca.isEmpty() -> Named(listOf(system()))
                else -> {
                    val ks = KeyStore.getInstance(KeyStore.getDefaultType())
                    ks.load(null, null)
                    val certs = certificates(ca, "ca")
                    certs.forEachIndexed { i, c -> ks.setCertificateEntry("ca$i", c) }
                    val f = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
                    f.init(ks)
                    val own = f.trustManagers.filterIsInstance<X509ExtendedTrustManager>().first()
                    Named(if (trustSystem) listOf(system(), own) else listOf(own))
                }
            }
            var kms: Array<javax.net.ssl.KeyManager>? = null
            if (certificate.isNotEmpty() || key.isNotEmpty()) {
                val chain = certificates(certificate, "certificate")
                val k = privateKey(key)
                matches(k, chain[0])
                val password = CharArray(0)
                val ks = KeyStore.getInstance(KeyStore.getDefaultType())
                ks.load(null, null)
                ks.setKeyEntry("client", k, password, chain.toTypedArray())
                val f = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm())
                f.init(ks, password)
                kms = f.keyManagers
            }
            val ctx = SSLContext.getInstance("TLS")
            ctx.init(kms, arrayOf(tm), null)
            return TLSPolicy(ctx.socketFactory, tm, if (insecure) HostnameVerifier { _, _ -> true } else null)
        }

        private fun system(): X509ExtendedTrustManager {
            val f = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
            f.init(null as KeyStore?)
            return f.trustManagers.filterIsInstance<X509ExtendedTrustManager>().first()
        }

        private fun certificates(pem: String, field: String): List<X509Certificate> {
            val blocks = Pem.blocks(pem).filter { it.first == "CERTIFICATE" }
            if (blocks.isEmpty()) throw IllegalArgumentException("tls: $field has no PEM certificate")
            val f = CertificateFactory.getInstance("X.509")
            return blocks.map {
                try {
                    f.generateCertificate(it.second.inputStream()) as X509Certificate
                } catch (e: CertificateException) {
                    throw IllegalArgumentException("tls: $field: a certificate can't be read (${e.message})")
                }
            }
        }

        /** The private key of pem: PKCS#8's PRIVATE KEY, RSA PRIVATE KEY (PKCS#1) or EC PRIVATE KEY (SEC 1), not encrypted. (An encrypted key, ENCRYPTED PRIVATE KEY, would be decrypted here.) */
        fun privateKey(pem: String): PrivateKey {
            for ((label, der) in Pem.blocks(pem)) {
                val (pkcs8, algs) = when (label) {
                    "PRIVATE KEY" -> der to listOf("RSA", "EC", "Ed25519", "EdDSA")
                    "RSA PRIVATE KEY" -> Der.seq(Der.int0 + Der.seq(Der.oid("1.2.840.113549.1.1.1") + Der.nul) + Der.tlv(0x04, der)) to listOf("RSA")
                    "EC PRIVATE KEY" -> {
                        val curve = Der.curveOfSEC1(der) ?: throw IllegalArgumentException("tls: key: an EC key of a curve that isn't P-256, P-384 or P-521")
                        Der.seq(Der.int0 + Der.seq(Der.oid("1.2.840.10045.2.1") + curve) + Der.tlv(0x04, der)) to listOf("EC")
                    }
                    else -> continue
                }
                for (alg in algs) {
                    try {
                        return KeyFactory.getInstance(alg).generatePrivate(PKCS8EncodedKeySpec(pkcs8))
                    } catch (e: Exception) {
                        // Another algorithm's, or none this system has.
                    }
                }
                throw IllegalArgumentException("tls: key can't be read")
            }
            throw IllegalArgumentException("tls: key has no PEM private key (PRIVATE KEY, RSA PRIVATE KEY or EC PRIVATE KEY)")
        }

        // matches throws unless key signs what the certificate's key verifies.
        private fun matches(key: PrivateKey, cert: X509Certificate) {
            val alg = when (key.algorithm) {
                "RSA" -> "SHA256withRSA"
                "EC" -> "SHA256withECDSA"
                else -> return
            }
            val ok = try {
                val data = "asuka".toByteArray()
                val s = Signature.getInstance(alg)
                s.initSign(key)
                s.update(data)
                val sig = s.sign()
                val v = Signature.getInstance(alg)
                v.initVerify(cert.publicKey)
                v.update(data)
                v.verify(sig)
            } catch (e: Exception) {
                false
            }
            if (!ok) throw IllegalArgumentException("tls: the key isn't the certificate's")
        }
    }
}

/**
 * The trust managers to try in turn (the system's, then ca's): the first
 * that trusts the server's chain does. A chain none trusts throws, saying
 * the host whose it was.
 */
private class Named(private val tms: List<X509ExtendedTrustManager>) : X509ExtendedTrustManager() {
    private inline fun first(host: String?, check: (X509ExtendedTrustManager) -> Unit) {
        var last: CertificateException? = null
        for (tm in tms) {
            try {
                check(tm)
                return
            } catch (e: CertificateException) {
                last = e
            }
        }
        val who = if (host.isNullOrEmpty()) "the server" else host
        throw CertificateException("$who's certificate isn't trusted: ${last?.message ?: "no authority trusted signed it"}", last)
    }

    override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String, socket: Socket?) =
        first((socket as? SSLSocket)?.handshakeSession?.peerHost) { it.checkServerTrusted(chain, authType, socket) }

    override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String, engine: SSLEngine?) =
        first(engine?.peerHost) { it.checkServerTrusted(chain, authType, engine) }

    override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String) = first(null) { it.checkServerTrusted(chain, authType) }

    override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String, socket: Socket?) = tms[0].checkClientTrusted(chain, authType, socket)
    override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String, engine: SSLEngine?) = tms[0].checkClientTrusted(chain, authType, engine)
    override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String) = tms[0].checkClientTrusted(chain, authType)
    override fun getAcceptedIssuers(): Array<X509Certificate> = tms.flatMap { it.acceptedIssuers.asList() }.toTypedArray()
}

/** Trusts every certificate: insecureSkipVerify's, for local tests only. */
private object Unchecked : X509ExtendedTrustManager() {
    override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String, socket: Socket?) {}
    override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String, engine: SSLEngine?) {}
    override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String) {}
    override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String, socket: Socket?) {}
    override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String, engine: SSLEngine?) {}
    override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String) {}
    override fun getAcceptedIssuers(): Array<X509Certificate> = arrayOf()
}

/** PEM's blocks: each one's label and its bytes (headers skipped). */
object Pem {
    fun blocks(text: String): List<Pair<String, ByteArray>> {
        val out = ArrayList<Pair<String, ByteArray>>()
        var label: String? = null
        val body = StringBuilder()
        for (raw in text.lines()) {
            val line = raw.trim()
            val l = label
            if (l != null) {
                if (line == "-----END $l-----") {
                    try {
                        out.add(l to Base64.getMimeDecoder().decode(body.toString()))
                    } catch (e: IllegalArgumentException) {
                        // Not base64: not a block.
                    }
                    label = null
                } else if (!line.contains(':')) {
                    body.append(line)
                }
            } else if (line.startsWith("-----BEGIN ") && line.endsWith("-----")) {
                label = line.substring(11, line.length - 5)
                body.setLength(0)
            }
        }
        return out
    }
}

/** DER, as much as a key's wrapping needs. */
object Der {
    fun tlv(tag: Int, content: ByteArray): ByteArray {
        val out = ByteArrayOutputStream()
        out.write(tag)
        val n = content.size
        if (n < 0x80) {
            out.write(n)
        } else {
            val len = ArrayList<Int>()
            var m = n
            while (m > 0) {
                len.add(0, m and 0xff)
                m = m shr 8
            }
            out.write(0x80 or len.size)
            len.forEach { out.write(it) }
        }
        out.write(content)
        return out.toByteArray()
    }

    fun seq(c: ByteArray) = tlv(0x30, c)
    val int0 = byteArrayOf(0x02, 0x01, 0x00)
    val nul = byteArrayOf(0x05, 0x00)

    fun oid(dotted: String): ByteArray {
        val arcs = dotted.split('.').map { it.toInt() }
        val out = ByteArrayOutputStream()
        out.write(arcs[0] * 40 + arcs[1])
        for (a in arcs.drop(2)) {
            val b = ArrayList<Int>()
            b.add(a and 0x7f)
            var m = a shr 7
            while (m > 0) {
                b.add(0, 0x80 or (m and 0x7f))
                m = m shr 7
            }
            b.forEach { out.write(it) }
        }
        return tlv(0x06, out.toByteArray())
    }

    // element is d[at]'s tag, its content's start and end, and where the next starts.
    private fun element(d: ByteArray, at: Int): IntArray? {
        if (at + 2 > d.size) return null
        val tag = d[at].toInt() and 0xff
        var n = d[at + 1].toInt() and 0xff
        var p = at + 2
        if (n and 0x80 != 0) {
            val k = n and 0x7f
            if (k == 0 || k > 4 || p + k > d.size) return null
            n = 0
            for (i in 0 until k) n = (n shl 8) or (d[p + i].toInt() and 0xff)
            p += k
        }
        if (p + n > d.size) return null
        return intArrayOf(tag, p, p + n)
    }

    /** The curve's OID (its whole element) of a SEC 1 ECPrivateKey: its [0] parameters, else the one its key's length says. */
    fun curveOfSEC1(d: ByteArray): ByteArray? {
        val top = element(d, 0) ?: return null
        if (top[0] != 0x30) return null
        var at = top[1]
        var keyLength = 0
        while (at < top[2]) {
            val e = element(d, at) ?: break
            if (e[0] == 0x04) keyLength = e[2] - e[1]
            if (e[0] == 0xa0) {
                val o = element(d, e[1])
                if (o != null && o[0] == 0x06) return d.copyOfRange(e[1], o[2])
            }
            at = e[2]
        }
        return when (keyLength) {
            32 -> oid("1.2.840.10045.3.1.7")
            48 -> oid("1.3.132.0.34")
            66 -> oid("1.3.132.0.35")
            else -> null
        }
    }
}
