// platform/agerange on Android: Google Play's Age Signals: the user is asked
// to share (Play's consent screen), then the age range is read; Play
// doesn't say how it knows (unknown).
package platform.agerange

import com.google.android.play.agesignals.AgeSignalsAccessRequest
import com.google.android.play.agesignals.AgeSignalsException
import com.google.android.play.agesignals.AgeSignalsManagerFactory
import com.google.android.play.agesignals.AgeSignalsRequest
import com.google.android.play.agesignals.model.AgeSignalsErrorCode
import com.google.android.play.agesignals.model.AgeSignalsStatus
import com.google.android.gms.tasks.Task
import dev.asuka.AsukaModules
import dev.asuka.AsukaUnsupportedException
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext

class PlaySignals : Signals {
    override suspend fun request(gates: List<Long>): Range = withContext(Dispatchers.Main) {
        val act = AsukaModules.activity ?: throw AsukaUnsupportedException("agerange: no activity shows the app")
        val m = AgeSignalsManagerFactory.create(act)
        val access = m.requestAgeSignalsAccess(AgeSignalsAccessRequest.builder().setActivity(act).build()).await()
        when (access.ageSignalsStatus()) {
            AgeSignalsStatus.NOT_SHARED -> Range(status = Status.DECLINED)
            AgeSignalsStatus.VERIFICATION_REQUIRED -> Range(status = Status.VERIFICATION_REQUIRED)
            else -> {
                val r = m.checkAgeSignals(AgeSignalsRequest.builder().build()).await()
                Range(status = Status.SHARED, lower = (r.ageLower() ?: 0).toLong(), upper = (r.ageUpper() ?: 0).toLong())
            }
        }
    }

    /** A Play task's result, its failure as the package's error where Play can't answer. */
    private suspend fun <T> Task<T>.await(): T = suspendCancellableCoroutine { cont ->
        addOnCompleteListener { t ->
            if (t.isSuccessful) cont.resume(t.result)
            else cont.resumeWithException(failed(t.exception))
        }
    }

    private fun failed(e: Exception?): Exception {
        val code = (e as? AgeSignalsException)?.errorCode
        return if (code == AgeSignalsErrorCode.API_NOT_AVAILABLE || code == AgeSignalsErrorCode.PLAY_STORE_NOT_FOUND ||
            code == AgeSignalsErrorCode.PLAY_SERVICES_NOT_FOUND || code == AgeSignalsErrorCode.APP_NOT_OWNED) AgeRangeException(AgeRangeError.NOT_AVAILABLE)
        else e ?: Exception("agerange: no age signals")
    }
}

fun makeSignals(): Signals = PlaySignals()
