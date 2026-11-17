// platform/network on Android: the default network's capabilities, from
// ConnectivityManager (ACCESS_NETWORK_STATE, in the package's manifest).
package platform.network

import android.annotation.SuppressLint
import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.os.Handler
import android.os.Looper
import dev.asuka.AsukaModules
import kotlinx.coroutines.awaitCancellation

@SuppressLint("MissingPermission")
class ConnectivityMonitor(context: Context) : Monitor {
    private val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    override fun current(): Status = status(cm.getNetworkCapabilities(cm.activeNetwork))

    override suspend fun watch(changed: (Status) -> Unit) {
        val main = Handler(Looper.getMainLooper())
        var last: Status? = null
        val report = { s: Status ->
            main.post {
                if (s != last) {
                    last = s
                    changed(s)
                }
            }
        }
        val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onCapabilitiesChanged(network: Network, c: NetworkCapabilities) { report(status(c)) }
            override fun onLost(network: Network) { report(Status()) }
        }
        report(current())
        cm.registerDefaultNetworkCallback(callback)
        try {
            awaitCancellation()
        } finally {
            cm.unregisterNetworkCallback(callback)
        }
    }

    private fun status(c: NetworkCapabilities?): Status {
        if (c == null || !c.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)) return Status()
        val type = when {
            c.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> ConnectionType.WIFI
            c.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> ConnectionType.CELLULAR
            c.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) -> ConnectionType.ETHERNET
            else -> ConnectionType.OTHER
        }
        return Status(
            connected = true,
            type = type,
            metered = !c.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED),
            constrained = cm.restrictBackgroundStatus == ConnectivityManager.RESTRICT_BACKGROUND_STATUS_ENABLED,
        )
    }
}

fun makeMonitor(): Monitor = ConnectivityMonitor(AsukaModules.context)
