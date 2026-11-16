// asuka/host on Android: the host's own modules (AsukaRuntime.callModule),
// on the main thread.
package asuka.host

import dev.asuka.AsukaRuntime
import dev.asuka.AsukaUnsupportedException
import dev.asuka.callModule
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.suspendCancellableCoroutine

class HostModules : Modules {
    override suspend fun call(module: String, method: String, args: ByteArray): ByteArray =
        suspendCancellableCoroutine { k ->
            val rt = AsukaRuntime.current
            if (rt == null) {
                k.resumeWithException(AsukaUnsupportedException("the host hasn't started"))
                return@suspendCancellableCoroutine
            }
            rt.callModule(module, method, args) { res ->
                if (k.isActive) res.fold({ k.resume(it) }, { k.resumeWithException(it) })
            }
        }

    override fun send(module: String, method: String, args: ByteArray) {
        AsukaRuntime.current?.callModule(module, method, args) { }
    }
}

fun makeModules(): Modules = HostModules()
