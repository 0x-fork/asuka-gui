// asuka/splash on Android: the host's splash screen (Android 12's), held
// over the first frame.
package asuka.splash

import dev.asuka.AsukaSplash

class HostLaunch : Launch {
    override fun hold() = AsukaSplash.hold()
    override fun hide() = AsukaSplash.release()
}

fun makeLaunch(): Launch = HostLaunch()
