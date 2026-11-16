// asuka/review on Android: Google Play's in-app review, which shows or
// not on Play's quota.
package asuka.review

import com.google.android.play.core.review.ReviewManagerFactory
import dev.asuka.AsukaModules
import dev.asuka.AsukaUnsupportedException
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext

class PlayReview : Store {
    override suspend fun request() = withContext(Dispatchers.Main) {
        val act = AsukaModules.activity ?: throw AsukaUnsupportedException("review: no activity shows the app")
        val m = ReviewManagerFactory.create(act)
        suspendCancellableCoroutine<kotlin.Unit> { cont ->
            m.requestReviewFlow().addOnCompleteListener { t ->
                if (!t.isSuccessful) cont.resumeWithException(t.exception ?: Exception("review: Play has no review here"))
                else m.launchReviewFlow(act, t.result).addOnCompleteListener { if (cont.isActive) cont.resume(kotlin.Unit) }
            }
        }
    }
}

fun makeStore(): Store = PlayReview()
