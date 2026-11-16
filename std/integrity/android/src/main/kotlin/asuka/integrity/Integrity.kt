// asuka/integrity on Android: Play Integrity's standard requests: prepare
// readies a token provider with the app's Cloud project number, token
// asks it for a request's verdict. App Attest is Apple's.
package asuka.integrity

import android.content.Context
import com.google.android.gms.tasks.Task
import com.google.android.play.core.integrity.IntegrityManagerFactory
import com.google.android.play.core.integrity.StandardIntegrityManager
import dev.asuka.AsukaModules
import dev.asuka.AsukaUnsupportedException
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.suspendCancellableCoroutine

class PlayIntegrity(private val app: Context) : Attest {
    @Volatile private var provider: StandardIntegrityManager.StandardIntegrityTokenProvider? = null

    override fun supported(): Boolean = true

    private fun apple(): Nothing = throw AsukaUnsupportedException("integrity: App Attest is Apple's: use prepare and token on Android")

    override suspend fun generateKey(): String = apple()
    override suspend fun attestKey(keyId: String, challenge: ByteArray): ByteArray = apple()
    override suspend fun assertion(keyId: String, data: ByteArray): ByteArray = apple()

    override suspend fun prepare(cloudProjectNumber: Long) {
        provider = IntegrityManagerFactory.createStandard(app)
            .prepareIntegrityToken(StandardIntegrityManager.PrepareIntegrityTokenRequest.builder().setCloudProjectNumber(cloudProjectNumber).build())
            .await()
    }

    override suspend fun token(requestHash: String): String {
        val p = provider ?: throw IllegalStateException("integrity: call prepare first")
        return p.request(StandardIntegrityManager.StandardIntegrityTokenRequest.builder().setRequestHash(requestHash).build()).await().token()
    }

    private suspend fun <T> Task<T>.await(): T = suspendCancellableCoroutine { cont ->
        addOnCompleteListener { t ->
            if (t.isSuccessful) cont.resume(t.result)
            else cont.resumeWithException(t.exception ?: Exception("integrity: Play Integrity failed"))
        }
    }
}

fun makeAttest(): Attest = PlayIntegrity(AsukaModules.context)
