// asuka/audio on Windows (MCI, audio_windows.go) and Linux (GStreamer,
// audio_linux.go); macOS runs the package's Swift. A sound's methods
// come on the main thread, where the platform's player is touched.
package audio
