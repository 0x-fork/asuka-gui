# The standard library

The standard library is the `asuka/...` packages. They come with the
`asuka` CLI, so they need no `asuka add`: import one and use it.

```asuka
import asuka/http
import asuka/store

state units = store.read("units", "metric")
```

Some are part of the compiler: they're plain functions with no platform
code. The others are [native packages](native.md) like any other: each
platform runs them with its own APIs (URLSession on iOS and macOS,
Android's own classes, the browser's on the web, Go on Windows and
Linux), so they behave as they do for the platform's own apps.

| Package | What it has |
|---|---|
| `asuka/json` | `json.decode<T>(text)` and `json.encode(value, indent:)`: typed JSON, by the compiler's knowledge of `T` |
| `asuka/math` | `sqrt`, `pow`, `floor`, `ceil`, `round`, `sin`, `cos`, `atan2`, `clamp`, `random`, `randomInt`, `seed`, `pi` |
| `asuka/time` | `time.now()` (milliseconds since 1970), `time.monotonic()` (a `Duration` that only goes forward) |
| `asuka/http` | `http.get`, `http.post`, `http.postJSON`, `http.request`: requests through the platform's own client, so proxies, trusted certificates, caches and cookies work as the system's do. A `Response` has `status`, `url`, `headers`, `body`, `text()`, `json<T>()` and `ok()` |
| `asuka/network` | `network.status()` (tracked: `connected`, `type`, `metered`, `constrained`; it watches the network only while something reads it), `network.current()`, `network.changes()` (a stream). NWPathMonitor on iOS and macOS, ConnectivityManager on Android, the Network List Manager on Windows, GNetworkMonitor and NetworkManager on Linux, `navigator.onLine` and the Network Information API on the web |
| `asuka/store` | `store.read(key, default)`, `store.write(key, value)`, `store.remove(key)`: small values kept across launches as JSON, in the platform's preferences (UserDefaults, SharedPreferences, `localStorage`, a file on Windows and Linux) |
| `asuka/audio` | `audio.play(source)`, and `audio.Player(source)`, a store with `play`, `pause`, `seek`, `setVolume`, `setRate`, `setLoop`, `status()` and `release()`. AVPlayer, Media3's ExoPlayer, `<audio>`, MCI, GStreamer ([Sound](device.md#sound-asukaaudio)) |
| `asuka/nowplaying` | `nowplaying.NowPlaying`: what the app plays, on the lock screen and the system's media controls, and the commands they send back (the media keys, the headphones'), following an `audio.Player` if given one ([Now Playing](device.md#now-playing-and-the-media-keys-asukanowplaying)) |
| `asuka/speech` | `speech.speak(text, options:)`, `stop`, `pause`, `resume`, `speaking`, `voices()`: text to speech, in the system's voices ([Text to speech](device.md#text-to-speech-asukaspeech)) |
| `asuka/record` | `record.audio(maxDuration:, quality:, background:)` and `stopAudio`, `pauseAudio`, `resumeAudio`, `audioStatus()`: sound from the microphone; `record.start()` and `stop()`: video from the `camera.View` on screen ([Recording sound](device.md#recording-sound-asukarecord)) |
| `asuka/camera` | `camera.View` (`facing`, `mode`, `scan`, `codes`; `code`, `error`): the camera, live; `camera.capture()`, `camera.take()` (the system's camera screen), `camera.record()` ([The camera](device.md#the-camera-asukacamera)) |
| `asuka/scan` | `scan.file(path, kinds:)`: the barcodes and QR codes in an image ([Barcodes](device.md#barcodes-and-qr-codes-asukascan)) |
| `asuka/vision` | `vision.analyze(path, text:, faces:, codes:, languages:)`: an image's text, line by line and word by word, faces and codes, each where it is, on the device ([What's in an image](device.md#whats-in-an-image-asukavision)) |
| `asuka/transcribe` | `transcribe.check(language)`, `transcribe.file(path, language:)`: a recording as text, on the device only (`notAvailable` where it can't be: the web, Linux, older Android) ([Speech to text](device.md#speech-to-text-asukatranscribe)) |
| `asuka/imageedit` | `imageedit.edit(source, actions, format:, quality:)` (resize, crop, rotate, flip, fill, pixelate), `imageedit.size`, `imageedit.videoFrame`: images and video frames, written as JPEG or PNG ([Editing images](device.md#editing-images-and-video-frames-asukaimageedit)) |
| `asuka/maps` | `maps.View` (`bind region`, `markers`, `mapType`, `showsUser`, `interactive`; `markerPress`, `press`, `regionChange`): Apple Maps, Google Maps, OpenStreetMap ([Maps](device.md#maps-asukamaps)) |
| `asuka/lottie` | `lottie.View(source)` (`bind playing`, `bind progress`, `loop`, `speed`, `segment`, `marker`, `fit`, `overrides`; `complete`, `loaded`): Lottie and dotLottie with each platform's player; nothing yet on Windows and Linux ([Lottie](components.md#lottie-animations)) |
| `asuka/svg` | `svg.Icon(markup)`, parsed when the app compiles, and `svg.View(icon) { color }`; `svg.Shape(layers)`: paths the app draws, which follow their box ([SVG icons](components.md#svg-icons)) |
| `asuka/chart` | `chart.View(series) { labels, label, low, high, grid }`; `chart.Series(name:, kind: .bar \| .line \| .area \| .points, values:, smooth:, color:)`: in Asuka, drawn with `svg.Shape` ([Charts](components.md#charts)) |
| `asuka/shader` | `shader.Program(source)`, a Kage shader compiled with the app for every platform, and `shader.Shader(program) { uniforms, animated }` ([Shaders](components.md#shaders)) |
| `asuka/markdown` | `markdown.View(text) { streaming }` and `markdown.heal(text)`: Markdown as the platform's styled text, healed while it streams in ([Markdown](components.md#markdown)) |

Where a platform has no code for a native package (an app run by
`asuka test`, which has none), what needs it throws an `unsupported`
error; `asuka/store` keeps nothing then, and reads give their defaults.

## Packages that aren't here

What the standard library doesn't have comes from a
[package](packages.md) (`asuka add`), or from a native package of your
own ([Native code](native.md)) when it's a platform API nothing wraps.
