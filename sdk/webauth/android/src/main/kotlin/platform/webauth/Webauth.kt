// platform/webauth on Android: a Custom Tab (the browser's: its cookies,
// passkeys and autofill; ephemeral where the browser has ephemeral
// tabs). It ends with a link of the callback scheme (the app's, which
// opens the app: the host hands it here before the app sees it), or as
// the user comes back to the app without one (they closed the tab), or
// as the task is cancelled (the app comes back over the tab).
package platform.webauth

import android.content.Intent
import android.net.Uri
import androidx.browser.customtabs.CustomTabsIntent
import dev.asuka.AsukaModules
import dev.asuka.AsukaUnsupportedException
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext

class CustomTabSheet : Sheet {
    override suspend fun open(url: String, callbackScheme: String, ephemeral: Boolean): String = withContext(Dispatchers.Main) {
        val u = Uri.parse(url)
        if (u.scheme != "https" && u.scheme != "http") throw IllegalArgumentException("webauth: not a web URL: $url")
        val act = AsukaModules.activity ?: throw AsukaUnsupportedException("webauth: no activity shows the app")
        suspendCancellableCoroutine { cont ->
            var away = false
            var stops = listOf<() -> kotlin.Unit>()
            fun end() { stops.forEach { it() } }
            stops = listOf(
                AsukaModules.watchURLs { opened ->
                    if (callbackScheme.isEmpty() || !Uri.parse(opened).scheme.equals(callbackScheme, ignoreCase = true)) return@watchURLs false
                    end()
                    if (cont.isActive) cont.resume(opened)
                    true
                },
                AsukaModules.watchLifecycle { resumed ->
                    if (!resumed) away = true
                    else if (away) {
                        // Back without the callback: the tab was closed.
                        end()
                        if (cont.isActive) cont.resumeWithException(WebauthException(WebauthError.CANCELED))
                    }
                },
            )
            cont.invokeOnCancellation {
                end()
                // The app again, over the tab (in its task): it closes.
                act.runOnUiThread {
                    act.startActivity(Intent(act, act.javaClass).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP))
                }
            }
            val tab = CustomTabsIntent.Builder()
            if (ephemeral) tab.setEphemeralBrowsingEnabled(true)
            tab.build().launchUrl(act, u)
        }
    }
}

fun makeSheet(): Sheet = CustomTabSheet()
