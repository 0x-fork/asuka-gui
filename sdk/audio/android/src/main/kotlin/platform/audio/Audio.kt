// platform/audio on Android: Media3's ExoPlayer, with music's audio
// attributes (it takes the audio focus as it plays). On the main thread.
package platform.audio

import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.exoplayer.ExoPlayer
import dev.asuka.AsukaModules
import dev.asuka.AsukaReleasing

private class ExoSound : Sound, AsukaReleasing {
    private var player: ExoPlayer? = null

    override fun open(source: String) {
        close()
        val p = ExoPlayer.Builder(AsukaModules.context)
            .setAudioAttributes(AudioAttributes.Builder().setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MUSIC).build(), true)
            .build()
        p.setMediaItem(MediaItem.fromUri(AsukaModules.uri(source)))
        p.prepare()
        player = p
    }

    override fun play() {
        val p = player ?: return
        if (p.playbackState == Player.STATE_ENDED) p.seekTo(0)
        p.play()
    }

    override fun playOnce() {
        val p = player ?: return
        playingOnce.add(this)
        p.addListener(object : Player.Listener {
            override fun onPlaybackStateChanged(state: Int) {
                if (state == Player.STATE_ENDED) {
                    close()
                    playingOnce.remove(this@ExoSound)
                }
            }
        })
        p.seekTo(0)
        p.play()
    }

    override fun pause() { player?.pause() }
    override fun seek(seconds: Double) { player?.seekTo((seconds * 1000).toLong()) }
    override fun setVolume(volume: Double) { player?.volume = volume.toFloat() }
    override fun setRate(rate: Double) { player?.setPlaybackSpeed(rate.toFloat()) }
    override fun setLoop(on: Boolean) { player?.repeatMode = if (on) Player.REPEAT_MODE_ONE else Player.REPEAT_MODE_OFF }

    override fun status(): Status {
        val p = player ?: return Status()
        val duration = p.duration
        return Status(
            position = p.currentPosition / 1000.0,
            duration = if (duration == C.TIME_UNSET) 0.0 else duration / 1000.0,
            playing = p.isPlaying,
            ended = p.playbackState == Player.STATE_ENDED,
        )
    }

    override fun close() {
        player?.release()
        player = null
    }

    // Let go: it stops, unless it's playing once.
    override fun released() {
        if (this !in playingOnce) close()
    }

    companion object {
        /** The sounds playing once, kept until they end. */
        val playingOnce = HashSet<ExoSound>()
    }
}

fun makeSound(): Sound = ExoSound()
