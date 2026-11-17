package dev.asuka

import android.annotation.SuppressLint
import android.content.Context
import android.view.View
import android.widget.FrameLayout

/** What a Video's player reports: the video's size, its end, an error. */
interface AsukaVideoEvents {
    fun loaded(width: Double, height: Double)
    fun ended()
    fun failed(message: String)
}

/**
 * A Video's player (platform/android/asuka-video: Media3's ExoPlayer),
 * which only the apps that show a Video are built with.
 */
interface AsukaVideoPlayer {
    val view: View
    var autoPlay: Boolean
    var loop: Boolean
    var muted: Boolean
    /** Plays source: an http(s) URL, a file, or one of the app's assets. */
    fun load(source: String)
    fun setFill(fill: Boolean)
    fun setControls(on: Boolean)
    fun play()
    fun pause()
    fun seek(seconds: Double)
    /** Frees the player: the view is destroyed. */
    fun release()
}

/**
 * A Video: the player of asuka-video, made by name as the first Video
 * shows (the CLI builds the module into an app whose sources name Video,
 * and into asuka dev's player). Without it, a Video's source fails.
 */
@SuppressLint("ViewConstructor")
class AsukaVideo(private val rt: AsukaRuntime, private val asukaID: Int) : FrameLayout(rt.themed), AsukaVideoEvents {
    val player: AsukaVideoPlayer? = make(rt.themed, this)?.also {
        addView(it.view, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
    }

    override fun loaded(width: Double, height: Double) = rt.emit(asukaID, AsukaEventKind.LOADED) { AsukaEvents.loaded(asukaID, width, height) }
    override fun ended() = rt.emit(asukaID, AsukaEventKind.ENDED) { AsukaEvents.ended(asukaID) }
    override fun failed(message: String) = rt.emit(asukaID, AsukaEventKind.FAILED) { AsukaEvents.failed(asukaID, message) }

    fun load(source: String) {
        val p = player
        if (p != null) p.load(source)
        else if (source.isNotEmpty()) failed("Video isn't in this build: the app's sources don't name it")
    }

    fun release() = player?.release()

    companion object {
        private var maker: java.lang.reflect.Constructor<*>? = null
        private var looked = false

        private fun make(context: Context, events: AsukaVideoEvents): AsukaVideoPlayer? {
            if (!looked) {
                looked = true
                maker = try {
                    Class.forName("dev.asuka.video.AsukaExoVideo").getDeclaredConstructor(Context::class.java, AsukaVideoEvents::class.java)
                } catch (_: ReflectiveOperationException) {
                    null
                }
            }
            return maker?.newInstance(context, events) as? AsukaVideoPlayer
        }
    }
}
