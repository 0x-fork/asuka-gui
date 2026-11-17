package dev.asuka.video

import android.annotation.SuppressLint
import android.content.Context
import android.net.Uri
import android.view.View
import android.widget.FrameLayout
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.Tracks
import androidx.media3.common.VideoSize
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.ui.AspectRatioFrameLayout
import androidx.media3.ui.PlayerView
import dev.asuka.AsukaVideoEvents
import dev.asuka.AsukaVideoPlayer
import java.io.File

/**
 * A Video's player: Media3's ExoPlayer in its PlayerView (its controls),
 * reporting the video's size, its end and its errors to the host's
 * AsukaVideo, which makes it by name.
 */
@SuppressLint("ViewConstructor")
@androidx.annotation.OptIn(UnstableApi::class)
class AsukaExoVideo(context: Context, private val events: AsukaVideoEvents) : FrameLayout(context), AsukaVideoPlayer {
    private val app = context.applicationContext
    private val player: ExoPlayer = ExoPlayer.Builder(app).build()
    private val playerView = (android.view.LayoutInflater.from(context).inflate(R.layout.asuka_player, this, false) as PlayerView).apply {
        player = this@AsukaExoVideo.player
        setShowBuffering(PlayerView.SHOW_BUFFERING_WHEN_PLAYING)
    }
    override val view: View get() = this
    override var autoPlay = false
        // The props go in the order the app wrote them, so Source may be
        // applied before AutoPlay: what's loaded plays when it comes.
        set(v) {
            if (v && !field && player.mediaItemCount > 0) player.playWhenReady = true
            field = v
        }
    override var loop = false
        set(v) { field = v; player.repeatMode = if (v) Player.REPEAT_MODE_ONE else Player.REPEAT_MODE_OFF }
    override var muted = false
        set(v) { field = v; player.volume = if (v) 0f else 1f }

    init {
        addView(playerView, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        player.addListener(object : Player.Listener {
            override fun onVideoSizeChanged(size: VideoSize) {
                if (size.width == 0 || size.height == 0) return
                report(size.width * size.pixelWidthHeightRatio.toDouble(), size.height.toDouble())
            }

            // The size from the track, known before the video draws: a
            // view of no size (until it's known) has no surface to draw on.
            override fun onTracksChanged(tracks: Tracks) {
                for (g in tracks.groups) {
                    if (g.type != C.TRACK_TYPE_VIDEO || !g.isSelected) continue
                    for (i in 0 until g.length) {
                        if (!g.isTrackSelected(i)) continue
                        val f = g.getTrackFormat(i)
                        if (f.width <= 0 || f.height <= 0) continue
                        val turned = f.rotationDegrees == 90 || f.rotationDegrees == 270
                        val w = f.width * f.pixelWidthHeightRatio.toDouble()
                        val h = f.height.toDouble()
                        if (turned) report(h, w) else report(w, h)
                        return
                    }
                }
            }

            override fun onPlaybackStateChanged(state: Int) {
                if (state == Player.STATE_ENDED) events.ended()
            }

            // Looping, an end is a jump back to the start.
            override fun onPositionDiscontinuity(old: Player.PositionInfo, new: Player.PositionInfo, reason: Int) {
                if (reason == Player.DISCONTINUITY_REASON_AUTO_TRANSITION) events.ended()
            }

            override fun onPlayerError(e: PlaybackException) = events.failed(e.message ?: e.errorCodeName)
        })
    }

    private var reported = 0.0 to 0.0

    private fun report(w: Double, h: Double) {
        if (reported == w to h) return
        reported = w to h
        events.loaded(w, h)
    }

    override fun load(source: String) {
        reported = 0.0 to 0.0
        if (source.isEmpty()) { player.clearMediaItems(); return }
        val uri = when {
            source.startsWith("http://") || source.startsWith("https://") || source.startsWith("file://") -> Uri.parse(source)
            source.startsWith("/") -> Uri.fromFile(File(source))
            // In development `asuka dev` pushes the assets into the app's files.
            File(app.filesDir, "asuka/assets/$source").exists() -> Uri.fromFile(File(app.filesDir, "asuka/assets/$source"))
            else -> Uri.parse("asset:///$source")
        }
        player.setMediaItem(MediaItem.fromUri(uri))
        player.prepare()
        player.playWhenReady = autoPlay
    }

    override fun setFill(fill: Boolean) {
        playerView.resizeMode = if (fill) AspectRatioFrameLayout.RESIZE_MODE_ZOOM else AspectRatioFrameLayout.RESIZE_MODE_FIT
    }

    override fun setControls(on: Boolean) { playerView.useController = on }

    override fun play() = player.play()
    override fun pause() = player.pause()
    override fun seek(seconds: Double) = player.seekTo((seconds * 1000).toLong())

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        player.pause() // gone from the screen (popped, dismissed)
    }

    override fun release() = player.release()
}
