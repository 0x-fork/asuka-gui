// asuka/host on Android: the host's own modules (AsukaHostModules), on
// the main thread.
package asuka.host

import dev.asuka.AsukaHostModules
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.suspendCancellableCoroutine

class HostModules : Modules {
    override suspend fun call(module: String, method: String, args: ByteArray): ByteArray =
        suspendCancellableCoroutine { k ->
            AsukaHostModules.call(module, method, args) { res ->
                if (k.isActive) res.fold({ k.resume(it) }, { k.resumeWithException(it) })
            }
        }

    override fun send(module: String, method: String, args: ByteArray) {
        AsukaHostModules.call(module, method, args) { }
    }
}

fun makeModules(): Modules = HostModules()
