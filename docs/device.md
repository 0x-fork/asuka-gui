# Device and services

Asuka's packages reach what the device has beyond views: the camera, codes,
what's in an image, photos and files, previewing, printing and PDFs, the
mail and message composers, location, notifications and push, sound, Now Playing and recording,
the network's state, the shared library, work in the background, shares
from other apps, JavaScript, sign-in and biometrics, the screen, maps,
the stores' services, and crash reports.
This page covers each package: what it does, its API, the permissions it
brings and how it behaves on each platform.

A package has one API everywhere. iOS and Android have nearly all of
it. On macOS, Windows and Linux the host answers each package with the
desktop's own services, and in a browser with the web's APIs. Where a
platform has nothing to offer, the package answers as on a phone
without it: `ErrNotAvailable`, an error that names what's missing, or a
call that does nothing. Each package's section says what it does and
what each platform does it with. Each desktop and the browser also has a
section of its own ([On a Mac](#on-a-mac), [On Windows](#on-windows),
[On Linux](#on-linux), [In a browser](#in-a-browser)): what an app needs
set up there, and a table of where it differs from iOS and Android.

## Permissions and purpose strings

An app that imports a package is given what the package needs: Android
permissions in the manifest, iOS purpose strings in Info.plist, and the
platform code itself. An app that doesn't import it goes without, so a
photo app doesn't ask for the microphone. A Mac app is given the same
purpose strings and the entitlements they need; Windows, Linux and the
web need nothing declared (see each one's section below).

| Package | Android | iOS |
| --- | --- | --- |
| `asuka/camera` | `CAMERA`, `RECORD_AUDIO` | `NSCameraUsageDescription`, `NSMicrophoneUsageDescription` |
| `asuka/location` | `ACCESS_COARSE_LOCATION`, `ACCESS_FINE_LOCATION`; for an app that calls `RequestAlways`, `ACCESS_BACKGROUND_LOCATION`; `StartUpdates`, `FOREGROUND_SERVICE_LOCATION` and a foreground service; `StartUpdates` or `Monitor`, `RECEIVE_BOOT_COMPLETED`, a receiver and Play services' location library | `NSLocationWhenInUseUsageDescription`; for an app that calls `RequestAlways`, `NSLocationAlwaysAndWhenInUseUsageDescription`; `StartUpdates`, the `location` background mode; `RequestPrecise`, `NSLocationTemporaryUsageDescriptionDictionary` |
| `asuka/notify` | `POST_NOTIFICATIONS` | |
| `asuka/network` | `ACCESS_NETWORK_STATE` | |
| `asuka/library` | `WRITE_EXTERNAL_STORAGE` (Android 9 and earlier) | `NSPhotoLibraryAddUsageDescription`; the app's Documents shown in Files |
| `asuka/background` | `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_DATA_SYNC`, `POST_NOTIFICATIONS`, `WAKE_LOCK`; a foreground service | |
| `asuka/scan` | ML Kit's barcode model (about 2 MB) | |
| `asuka/vision` | ML Kit's text, face and barcode models (about 30 MB) | |
| `asuka/photos` | `READ_MEDIA_IMAGES`, `READ_MEDIA_VISUAL_USER_SELECTED`; `READ_EXTERNAL_STORAGE` (Android 12 and earlier) | `NSPhotoLibraryUsageDescription` |
| `asuka/push` | Firebase Cloud Messaging, `POST_NOTIFICATIONS` | the `aps-environment` entitlement; `remote-notification` background mode |
| `asuka/appleauth` | | the Sign in with Apple entitlement |
| `asuka/pay` | Google Pay's library, its API enabled, its callbacks service | the Apple Pay entitlement, with `asuka.toml`'s `applePay.merchantIds` |
| `asuka/biometric` | `USE_BIOMETRIC` | `NSFaceIDUsageDescription` |
| `asuka/record` | `RECORD_AUDIO`, `FOREGROUND_SERVICE_MICROPHONE`; a foreground service | `NSMicrophoneUsageDescription` |
| `asuka/nowplaying` | `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_MEDIA_PLAYBACK`; a foreground service | the `audio` background mode (for an app that calls `nowplaying.Show`) |
| `asuka/liveactivity` | `POST_NOTIFICATIONS`, `POST_PROMOTED_NOTIFICATIONS` | `NSSupportsLiveActivities` and the widget extension (for an app that calls it) |
| `asuka/transcribe` | `RECORD_AUDIO` | `NSSpeechRecognitionUsageDescription` |
| `asuka/screen` | `DETECT_SCREEN_CAPTURE` | |
| `asuka/tracking` | | `NSUserTrackingUsageDescription` |
| `asuka/review` | Play's in-app review library | |
| `asuka/integrity` | Play Integrity | the App Attest entitlement |
| `asuka/agerange` | Play Age Signals | the Declared Age Range entitlement |
| `asuka/purchase` | Play Billing, `com.android.vending.BILLING` | StoreKit 2 (for an app that calls it); no entitlement |
| `asuka/icloud` | (none: no iCloud) | the key-value store's entitlement, iCloud Documents and the app's containers, `NSUbiquitousContainers`, as the app calls them ([iCloud](icloud.md)) |
| `asuka/maps` | Google's Maps SDK | |
| `asuka/periodic` | WorkManager | `fetch` and `processing` background modes; the task identifiers |
| `asuka/sensors` | `ACTIVITY_RECOGNITION` (the pedometer) | `NSMotionUsageDescription` (the pedometer) |
| `asuka/speech` | the text-to-speech engine's visibility (`<queries>`) | |
| `asuka/contacts` | `READ_CONTACTS` (for `List`) | `NSContactsUsageDescription` |
| `asuka/calendar` | `READ_CALENDAR` (for `Calendars`, `Events`) | `NSCalendarsFullAccessUsageDescription`, `NSCalendarsUsageDescription` |
| `asuka/compose` | the visibility of the apps for `mailto:` and `smsto:` (`<queries>`) | `mailto` in `LSApplicationQueriesSchemes` |
| `asuka/nfc` | `NFC`; `uses-feature android.hardware.nfc` (not required); asuka.toml's `nfc` filters ([NFC tags](nfc.md)) | `NFCReaderUsageDescription`; Core NFC's entitlement; asuka.toml's `nfc` AIDs and FeliCa system codes |
| `asuka/bluetooth` | `BLUETOOTH_SCAN` (`neverForLocation`), `BLUETOOTH_CONNECT`, `BLUETOOTH_ADVERTISE`; the scan's receiver for `asuka.toml`'s `bluetooth.background` (see [Bluetooth](bluetooth.md#what-the-build-adds)) | `NSBluetoothAlwaysUsageDescription`; `bluetooth-central`, `bluetooth-peripheral` for `bluetooth.background` |

`asuka/device`, `asuka/audio`, `asuka/splash`, `asuka/imageedit`,
`asuka/widget`, `asuka/js`, `asuka/secure`, `asuka/preview` and
`asuka/print` need none.

An entitlement needs its capability on the app's App ID
(developer.apple.com, Identifiers) for a release build: the provisioning
profile has it then. `asuka build --platform ios` stops with what's missing if the
profile lacks one; simulator builds have them all.

The purpose strings Asuka writes are generic ("To take a photo."). iOS
and macOS show them when they ask the user, so write your own in
`asuka.toml`'s `ios.usage`:

```toml
[ios.usage]
NSCameraUsageDescription = "To photograph your receipts."
NSLocationWhenInUseUsageDescription = "To find stores near you."
```

Each package asks the user the first time it needs to, not at launch.
Calls that wait (for the user, a location fix, the platform) are marked
"not on the main thread" below: call them from a goroutine and
`ui.Post` the result back (see [the main thread](concepts.md#the-main-thread)).
The others take a callback, called on the main thread.

## On a Mac

A Mac app (`asuka dev` and `asuka build`) has the same purpose strings in
its Info.plist (location's as `NSLocationUsageDescription` too). Its
signature has the hardened runtime's entitlements for what they ask
(camera, microphone, location, contacts, calendars, photos), without
which a Developer ID build is refused them. Push's `aps-environment`,
Sign in with Apple, App Attest, Apple Pay and iCloud are entitlements only a provisioning
profile grants: `asuka build` embeds one for an app that uses them
(`asuka.toml`'s `mac.profile`, else `ASUKA_MAC_PROFILE`, else the best
match installed). Without one, the app builds, the CLI says what won't
work, and on the Mac registering for pushes and signing in with Apple
fail, and App Attest says it isn't supported. `asuka dev` signs ad hoc,
without a profile, so the same holds there. A Mac App Store build
(`--appstore`) is sandboxed, with the network and the files the user
picks. See [Shipping](shipping.md).

The packages work on macOS as on iOS, through the same frameworks
(AVFoundation, PhotoKit, Core Location, MapKit, Vision, Speech,
UserNotifications, AuthenticationServices), except where a Mac differs:

| Package | On a Mac |
| --- | --- |
| `asuka/device` | `pickPhotos` and `pickFiles`: an open panel, a sheet on the window (no permission), copies in the app's caches. `browse` opens the page in the user's browser. `info`: `model` `"Mac"`, `system` `"macOS"` |
| `asuka/camera` | `Take`: a sheet over the window with a live preview, Take Photo and Cancel (AppKit has no camera screen). `View`'s "back" is the built-in camera (a Mac has no back one), or the one plugged in; scanning codes in it needs macOS 13 |
| `asuka/record` | No audio session: other apps' sound plays on; `Background` changes nothing, a Mac app records out of sight anyway |
| `asuka/nowplaying` | `MPNowPlayingInfoCenter` and `MPRemoteCommandCenter`, as on iOS: Control Center's and the menu bar's Now Playing, the keyboard's media keys, AirPods. A Mac needs no audio session or background mode |
| `asuka/contacts`, `asuka/calendar` | `Add` asks in an alert over the window (macOS has no new contact or event screen), then saves, asking for access first |
| `asuka/photos` | PhotoKit, as on iOS; a Mac has no limited library: `ManageLimited` does nothing |
| `asuka/library` | Photos and videos go to Photos; `Downloads` and `Music` to the user's `~/Downloads` and `~/Music`, in the folder asked |
| `asuka/biometric` | Touch ID's prompt, or the user's password (not with `noPasscode`) |
| `asuka/push` | APNs, as on iOS; no `Rewrite` (it needs a notification service extension). Needs a provisioning profile (above) |
| `asuka/appleauth` | Apple's button and sheet, as on iOS. Needs a provisioning profile (above) |
| `asuka/pay` | Apple Pay's sheet and button, as on iOS: a Mac with Touch ID pays, or one near the user's iPhone or Apple Watch. Needs a provisioning profile (above) |
| `asuka/integrity` | App Attest, as on iOS. Needs a provisioning profile (above): without one `supported()` is false |
| `asuka/background` | The task keeps App Nap from slowing the app; there's no notification, so `Update` changes nothing |
| `asuka/periodic` | `NSBackgroundActivityScheduler`: the tasks run while the app runs (it isn't launched for them); one that wants power waits while the Mac is on its battery |
| `asuka/screen` | `lock` and `setBars` do nothing; `preventCapture` leaves the app's windows out of screenshots and recordings; `screenshots()` says nothing |
| `asuka/transcribe` | On the device only; the app's bundle needs its purpose string, which `asuka build` writes |
| `asuka/sensors` | A Mac has no motion sensors: `Available` is false, watching gets nothing, `StepsBetween` fails with `ErrNotAvailable` |
| `ui.Haptic` | The trackpad's feedback (a Force Touch trackpad): an alignment for `HapticSelection`, a level change for the others |
| `asuka/widget`, `asuka/liveactivity`, `asuka/agerange` | Not on a Mac: `widget.Set` does nothing, `liveactivity.Start` fails with `ErrNotAvailable`, `agerange.request` with `AgeRangeError.notAvailable` |
| `asuka/purchase` | StoreKit 2, in the app's Swift library, which the build makes for an app that imports the package ([In-app purchases](purchases.md)) |
| `asuka/icloud` | As on iOS, in the app's Swift library. Needs a provisioning profile with iCloud (above): without one, `ErrNotAvailable` ([iCloud](icloud.md)) |
| `asuka/splash` | A Mac app has no launch screen: `hold` and `hide` do nothing |
| `ui.OnShare`, `app.ShareSheet`, shortcuts | iOS and Android only: a Mac app isn't offered in other apps' share menus, and `asuka.toml`'s `shortcuts` aren't added |

## On Windows

A Windows app declares nothing: `asuka build --platform windows` writes no
capabilities, and Windows asks a desktop app for no permission. The
privacy switches in Settings (Privacy & security) still hold for the
camera, the microphone and location: with one off, the camera and
recording fail with `"not allowed"`, and location with
`location.ErrDenied`.

The packages work on Windows through its own services (Media Foundation,
WIC, WinRT, Windows Hello, SAPI, the Credential Manager), except where
Windows differs; where it has nothing, the package answers as on a
device without it:

| Package | On Windows |
| --- | --- |
| `asuka/device` | `pickPhotos` and `pickFiles`: the open dialog over the window, copies in the app's caches. `browse` opens the page in the user's browser. The clipboard's images are PNGs. `keepAwake` keeps the display on. `info`: `model` `"PC"`, `system` `"Windows"`, `systemVersion` `"10.0.<build>"` |
| `asuka/secure` | The user's generic credentials in the Credential Manager, named `<app>/<key>` (a value up to 2560 bytes) |
| `asuka/camera` | Media Foundation's capture engine. `Take`: a dialog over the window with a live preview, Take Photo and Cancel (Enter and Escape). `View` picks the camera Windows names front or rear, else the first; with `Scan`, it reads codes in four frames a second with Asuka's own reader (as `asuka/scan`'s `File`). Settings' camera switch off: "not allowed" |
| `asuka/record` | H.264 and AAC in MPEG-4 (`.mp4`), the default microphone unless muted |
| `asuka/audio` | MCI (DirectShow's playback); a URL is downloaded to the app's caches first |
| `asuka/record` | The default microphone, AAC at 44.1 kHz (voice mono, music stereo); `Background` changes nothing |
| `asuka/nowplaying` | The app window's SystemMediaTransportControls (a hidden window of its own for an app without one): the volume flyout on Windows 10, quick settings' media controls on Windows 11, the lock screen, the keyboard's media keys. The skips are its fast-forward and rewind buttons; it has no toggle (the play/pause key sends play or pause) |
| `asuka/speech` | SAPI's voices, those installed (Settings, Time & language, Speech) |
| `asuka/network` | The Network List Manager: Wi-Fi, cellular or a cable; `Metered` for a connection with a cost, `Constrained` near or over its data limit |
| `asuka/location` | WinRT's Geolocator. `PlaceName` asks OpenStreetMap's Nominatim (Windows's own geocoder needs a Bing Maps key): one request a second at most, answers kept in the app's caches. Regions: the `GeofenceMonitor`, while the app runs; background updates computed from the positions |
| `asuka/biometric` | Windows Hello's prompt; `check()`'s kind is the sensor's; Hello always offers its PIN, and names its own cancel button |
| `asuka/webauth` | A window over the app's with WebView2 in it (the app's web profile; `ephemeral`: a throwaway one) |
| `asuka/js` | A WebView2 that's never shown |
| `asuka/contacts`, `asuka/calendar` | Windows gives the contacts and calendars to packaged apps only: `Request` says denied; `Add` opens a vCard or an event file in the user's contacts or calendar app (`saved` is false: it doesn't say) |
| `asuka/photos` | The Pictures folder (no permission to ask); screenshots are its Screenshots folder's |
| `asuka/library` | The user's Downloads, Videos, Music and Pictures folders, in the folder asked |
| `asuka/vision` | Windows's OCR, in the languages whose OCR is installed (Settings, Time & language), and its face detector; codes as `asuka/scan` reads them |
| `asuka/scan` | Windows has no reader of codes for apps: Asuka's own, in Go, reads QR codes, Data Matrix, EAN-13 and EAN-8, UPC-A and UPC-E, Code 128 and Code 39 (not Aztec, PDF417, Code 93, Codabar nor ITF) |
| `asuka/imageedit` | Windows's codecs read (HEIC and WebP where their extensions are installed); a video's frame through Media Foundation |
| `asuka/maps` | Windows has no map control for desktop apps (its own needs a Bing Maps key): the host draws OpenStreetMap's tiles, downloaded two at a time and kept a week in the app's caches, with "© OpenStreetMap contributors" on the map; drag, the wheel or a pinch, double-click, + and -. Satellite and Hybrid show the streets (no free imagery), and `showsUser` shows nothing, as on Linux |
| `asuka/background` | The task keeps the PC from sleeping; there's no notification, so `Update` changes nothing |
| `asuka/periodic` | Timers while the app runs (it isn't launched for them); one that wants power waits while the PC is on its battery |
| `asuka/screen` | `lock` and `setBars` do nothing; `preventCapture` leaves the app's windows out of screenshots and recordings; `screenshots()` says nothing |
| `asuka/tracking` | No prompt: allowed while the advertising ID is on in Settings, else denied |
| `asuka/push`, `asuka/widget`, `asuka/review`, `asuka/integrity` | Packaged (Store) apps' or Apple's and Google's: `push.Register` fails, `widget.Set` does nothing, `review.request` shows nothing, `integrity.supported()` is false |
| `asuka/liveactivity` | A phone's: `Enabled` is false, `Start` fails with `ErrNotAvailable` |
| `asuka/purchase` | The Microsoft Store, for an app installed from it; otherwise `Available` is false and the rest fail with `ErrNotAvailable` ([In-app purchases](purchases.md)) |
| `asuka/pay` | No wallet pays for a Windows app: `Available` says none, `Pay` fails with `ErrNotAvailable`, `Button` shows nothing |
| `asuka/appleauth`, `asuka/agerange`, `asuka/icloud`, `asuka/transcribe`, `asuka/sensors` | Not on Windows: unavailable, as on a device without them |
| `ui.Haptic` | A PC has no haptics: it does nothing |
| `asuka/splash` | A Windows app has no launch screen: `hold` and `hide` do nothing |
| `ui.OnShare`, `app.ShareSheet`, shortcuts | iOS and Android only |

## On Linux

A Linux app declares nothing either. Where the host goes through an XDG
desktop portal (files, the camera, location, notifications, mail,
printing, opening files and links, keeping the system awake), the
portal asks the user as it does, and it works in a Flatpak sandbox as
out of one. The Flatpak manifest `asuka build --platform linux` writes
lets the app reach only the display, the GPU, the network and the
portals. Services the host reaches without a portal (Speech Dispatcher,
Evolution Data Server for contacts and calendars, NetworkManager for the
connection's type, iio-sensor-proxy, a Video4Linux camera) need the
`finish-args` that open them, added to the manifest; without them, the
package answers as on a system without the service. A notification due
later needs systemd outside the sandbox: in a Flatpak it shows only
while the app runs (see [On each platform](#on-each-platform)).

The Linux host reaches the desktop's services through the portals where
there's one, GLib and GTK, and the libraries a desktop has, each loaded
the first time it's used: a system without it answers an error that
names what's missing.

| Package | On Linux |
| --- | --- |
| `asuka/device` | `pickPhotos` and `pickFiles`: GTK's file dialog (the portal's in a sandbox), copies in the app's caches. Clipboard images and changes through GDK's clipboard. `browse` opens the page in the user's browser. `keepAwake` keeps the session from idling. `info`: `model` `"PC"`, `system` `"Linux"`, `systemVersion` the distribution's (`/etc/os-release`) |
| `asuka/secure` | The keyring (libsecret: GNOME Keyring, KWallet, KeePassXC), an item per key labeled with the app's name; without libsecret or a keyring, the calls fail saying so |
| `asuka/network` | GNetworkMonitor (the portal's in a sandbox); Wi-Fi, ethernet or cellular from NetworkManager; never constrained |
| `asuka/location` | The location portal (GeoClue's, which asks the user), else GeoClue itself; `PlaceName` with geocode-glib, when it's installed. GeoClue has no geofences: regions and background updates are computed from the positions while the app runs |
| `asuka/speech` | Speech Dispatcher (started when it isn't running): its voices, rate, pitch, volume |
| `asuka/audio`, `asuka/record` | GStreamer: an AAC encoder (FDK, libav's...) records, the default source the microphone |
| `asuka/nowplaying` | An MPRIS player on the session bus, named `org.mpris.MediaPlayer2.<app>.instance<pid>` while something shows: GNOME's and KDE's media controls and lock screens, the media keys, `playerctl`. MPRIS's `Seek` is a skip where the app skips, else a seek; the artwork is a `file://` or http(s) URL |
| `asuka/library` | The user's folders (XDG's Pictures, Videos, Music, Downloads), in the folder asked |
| `ui.ShareFiles` | No share sheet: the files are copied to the clipboard, with a toast |
| `asuka/camera`, `asuka/record` | The camera portal (it asks the user once; PipeWire's cameras, `gstreamer1.0-pipewire`), else the first Video4Linux camera, through GStreamer. `Take`: a dialog over the window with a live preview, Take Photo and Cancel. `View`: the preview, mirrored, filling its box; front and back are the same camera (a computer's face the user). Videos are MPEG-4 (H.264 from x264 or OpenH264, AAC), else WebM; scanning codes needs GStreamer's `zbar` element. No camera: `ErrNoCamera`; refused: "not allowed" |
| `asuka/scan`, `asuka/vision` | ZBar (`libzbar0`) reads codes: no Aztec or Data Matrix. Tesseract (`libtesseract5` and a language's data) reads text; nothing finds faces: `Faces` is empty. Without ZBar, `scan.File` fails "not available"; `vision.Analyze` leaves out what isn't installed |
| `asuka/imageedit` | GdkPixbuf's loaders decode (HEIF and WebP where their loaders are installed), upright; the actions are done in Go; video frames from GStreamer |
| `asuka/webauth` | A dialog over the window with a WebKitGTK web view (`ephemeral`: a session of its own); closing it cancels |
| `asuka/js` | A WebKitGTK web view that's never shown |
| `asuka/maps` | libshumate's map of OpenStreetMap's tiles (`libshumate-1.0-1`); Satellite and Hybrid show the streets; no user location layer. Without libshumate, the view says so and `OnError` is called |
| `asuka/background` | The task keeps the system from suspending; no notification, `Update` changes nothing |
| `asuka/periodic` | The tasks run on a timer while the app runs (it isn't launched for them); one that wants power waits while on the battery, one that wants the network while it's offline |
| `asuka/screen` | `lock`, `setBars` and `preventCapture` do nothing; `screenshots()` says nothing |
| `asuka/sensors` | iio-sensor-proxy (convertibles, tablets): the light sensor, in lux, and the accelerometer's orientation as 1 g along the axis that's up, as it turns. No others: `Available` is false, `StepsBetween` fails with `ErrNotAvailable` |
| `asuka/contacts`, `asuka/calendar` | Evolution Data Server's address books and calendars (GNOME Contacts', GNOME Calendar's, the online accounts'): access is `Full` when it has one; `Pick` is a dialog of the contacts with a search field; `Add` asks, then saves in the default address book or the event's calendar. Without it: contacts' access is `Denied`, calendars' `WriteOnly`; `Add` opens a vCard or an iCalendar file in the user's contacts or calendar app (`saved` is false); `contacts.Pick` fails |
| `asuka/photos`' library, `asuka/appleauth`, `asuka/transcribe`, `asuka/widget`, `asuka/liveactivity` | None on Linux: the library's access is `Denied`; `appleauth.Available` is false and its button shows nothing; `transcribe` and `liveactivity.Start` fail with `ErrNotAvailable`; `widget.Set` does nothing. `asuka/splash` has nothing to hold |
| `asuka/purchase` | No store: `Available` is false, the rest fail with `ErrNotAvailable` |
| `asuka/icloud` | No iCloud: `ErrNotAvailable` |
| `asuka/biometric`, `asuka/integrity`, `asuka/agerange`, `asuka/push` | None on Linux: not available, not supported. `asuka/review` does nothing; `asuka/tracking` answers allowed |
| `asuka/pay` | No wallet pays for a Linux app: `Available` says none, `Pay` fails with `ErrNotAvailable`, `Button` shows nothing |
| `asuka/crash` | Go's crash output, which has a crash in native code too (a signal in GTK's code, a GLib abort), reported as `Native`; the system's own record (systemd-coredump, apport) isn't read |
| `ui.Haptic` | A desktop has no haptics: it does nothing |
| `ui.OnShare`, `app.ShareSheet`, shortcuts | iOS and Android only |

## In a browser

A page declares nothing: the browser asks the user the first time a
package needs the camera, the microphone, location or notifications.
Most of these APIs need a secure page, served over `https` or from
`localhost` (`asuka dev --platform web`'s, but not `--lan`'s addresses).
The packages call the web's APIs; [the web](web.md) has the details,
and its service worker for notifications and Web Push.

| Package | In a browser |
| --- | --- |
| `asuka/device` | `pickPhotos` and `pickFiles`: the browser's file picker, copies in the caches. `browse`: a new tab. The clipboard: the app's own copies, and other apps' where the user lets the page read it. `keepAwake`: the Screen Wake Lock. `info` from the user agent (Chromium's client hints give the system's version and an Android phone's model) |
| `asuka/secure` | The values encrypted (AES-GCM) in IndexedDB, with a key the browser won't let out; not a keychain (see [the web](web.md)) |
| `asuka/camera`, `asuka/record` | `Take` opens a phone's camera, or a dialog with the live camera and a shutter; `View` is the camera in a `<video>`, with `Capture`, recording, and scanning codes where the browser has `BarcodeDetector` |
| `asuka/scan`, `asuka/vision` | `BarcodeDetector` reads codes; faces and text only where the browser has them |
| `asuka/imageedit` | A canvas: JPEG or PNG; a remote image needs CORS |
| `asuka/photos` | A page can't read the library: access is `Denied` (`device.PickPhotos` picks photos) |
| `asuka/library` | `Save` downloads the file |
| `asuka/location` | The Geolocation API; `PlaceName` isn't on the web (a browser has no geocoder). Regions and background updates are computed from the positions, only while the page is open |
| `asuka/network` | Online from `navigator.onLine`; Wi-Fi, cellular or Ethernet where the browser says (Chrome on Android), metered for cellular, constrained for Data Saver |
| `asuka/notify`, `asuka/push` | The Notification API and Web Push, through the app's service worker (`push.Register`'s `Service` is `"webpush"`); a notification due later can't show while no page of the app is open |
| `asuka/audio`, `asuka/record` | An audio element per player (the browser's autoplay rules apply: play from a tap); `MediaRecorder`, AAC in MPEG-4 where the browser records it, else Opus in WebM |
| `asuka/nowplaying` | The Media Session API (`navigator.mediaSession`): the browser's media controls, the media keys, a phone's lock screen, shown while the page plays sound. It has no toggle (the key sends play or pause), and a browser without an action (stop, in some) leaves it out |
| `asuka/speech` | `speechSynthesis`, the browser's voices |
| `asuka/sensors` | The Generic Sensor API (Chrome) or device motion and orientation events (Safari asks on a tap), in Android's axes and units; no barometer or pedometer |
| `asuka/biometric` | The platform's authenticator through WebAuthn (Touch ID, Face ID, Windows Hello, Android's screen lock): the first `authenticate` makes a passkey for the site. The prompt is the browser's: `reason` and `cancel` don't show |
| `asuka/webauth` | A popup, done when it comes back to a page of the app's own origin (a custom scheme can't open a page) |
| `asuka/js` | A hidden, sandboxed frame |
| `asuka/contacts`, `asuka/calendar` | `contacts.Pick` where the browser has the Contact Picker (Chrome on Android); `Add` downloads a vCard or an `.ics` file; reading them isn't on the web |
| `asuka/screen` | `lock` only full screen or in an installed app; hiding the bars is full screen; no `preventCapture` or `screenshots()` |
| `asuka/background` | The page runs while it's open; a task adds nothing, and has no notification |
| `asuka/tracking` | No prompt: `.denied` when the browser sends Global Privacy Control or Do Not Track, else `.allowed` |
| `ui.Haptic` | `navigator.vibrate`, where the browser has it (Chrome on Android) |
| `asuka/review`, `asuka/integrity`, `asuka/agerange`, `asuka/appleauth`, `asuka/icloud`, `asuka/periodic`, `asuka/widget`, `asuka/liveactivity`, `asuka/transcribe` | Not on the web: an error that says so, or the package's `ErrNotAvailable` |
| `asuka/purchase` | Google Play through the Digital Goods API, in a Trusted Web Activity; elsewhere no store (`ErrNotAvailable`) |
| `asuka/maps` | OpenStreetMap's tiles, drawn by the host, as on Windows; `showsUser` once the page has the location permission |
| `asuka/pay` | Apple Pay where the browser has it (Safari), through the Payment Request API, the merchant validated by the app's server (`OnValidateMerchant`); Google Pay elsewhere, through Google's web API (pay.js); each one's own button. A sheet shows only from a tap |
| `asuka/crash` | Go's crashes (the host keeps the runtime's output) and uncaught JavaScript errors, as `Exception` reports that aren't fatal |
| `asuka/splash` | `hold` and `hide` do nothing |
| `ui.OnShare`, `app.ShareSheet`, shortcuts | iOS and Android only |

## The camera: asuka/camera

`camera.View` shows what the camera sees, live: AVFoundation's preview
on iOS and the Mac, CameraX on Android, `getUserMedia` in a browser,
Media Foundation on Windows, GStreamer on Linux. `camera.capture()` takes
a photo from the view on screen; `camera.take()` shows the system's
camera screen instead (iOS's, Android's camera app; a sheet with a
preview on the desktop, the file picker's camera in a browser) and gives
the photo's file, or `none` if the user cancelled. Photos are JPEGs in
the app's caches: move one to keep it.

```asuka
import asuka/camera

component Shutter {
  state facing: camera.Facing = .back
  state last = ""
  state status = ""

  Column {
    camera.View {
      facing: facing
      grow: 1
      on error(m) { status = m }
    }
    Row {
      gap: 12
      Button("Flip") { on press { facing = if facing == .back { .front } else { .back } } }
      Button("Take") { on press { last = try await camera.capture() } }
    }
    Text(status)
  }
}
```

The view asks for the camera the first time it shows; refused, or on a
device without one (the iOS simulator), it reports `error`, and
`capture` throws `CameraError.notAllowed` or `.noCamera` (`.noView` when
no `camera.View` is on screen). An app that imports `asuka/camera` is
given the camera's and the microphone's purpose strings on iOS (set your
own in asuka.toml's `[ios.usage]`) and the `CAMERA` and `RECORD_AUDIO`
permissions on Android.

## Recording video: asuka/record

A `camera.View` in video mode records: `record.start()` records what it
shows, with the microphone unless `mute`, until `record.stop()` (or
`maxDuration`), and gives the file, an MPEG-4 video in the app's caches.
`camera.record` and `camera.stopRecording` are the same.

```asuka
import asuka/camera
import asuka/record

component Clip {
  state recording = false
  state file = ""

  Column {
    camera.View { mode: .video; grow: 1 }
    Button(if recording { "Stop" } else { "Record" }) {
      on press {
        if recording {
          record.stop()
        } else {
          recording = true
          defer { recording = false }
          file = try await record.start(maxDuration: 30s)
        }
      }
    }
  }
}
```

AVCaptureMovieFileOutput on iOS and the Mac, CameraX's `VideoCapture` on
Android, `MediaRecorder` in a browser (WebM where it can't write MPEG-4),
Media Foundation's sink writer on Windows, GStreamer on Linux.

## Barcodes and QR codes: asuka/scan

A `camera.View` with `scan` on reads the codes it sees, live, and reports
each with `code` (once while it stays in sight). `codes` limits the kinds
it looks for; none: every kind. Kinds are named `"qr"`, `"aztec"`,
`"datamatrix"`, `"pdf417"`, `"ean13"`, `"ean8"`, `"upca"`, `"upce"`,
`"code128"`, `"code39"`, `"code93"`, `"codabar"` and `"itf"`.

`scan.file(path)` reads the codes in an image file instead (a photo
taken or picked): Vision on iOS and the Mac, ML Kit on Android (its
model in the app, about 2 MB, so it works without Google Play services),
the browser's `BarcodeDetector` where it has one, a Go decoder on
Windows, ZBar's GStreamer element on Linux.

```asuka
import asuka/camera
import asuka/scan

component Scanner {
  state found = ""

  Column {
    camera.View {
      scan: true
      codes: ["qr", "ean13"]
      grow: 1
      on code(c) { found = "{c.kind}: {c.value}" }
    }
    Text(found)
  }
}

async fn read(photo: string) throws -> [string] {
  var out: [string] = []
  for c in try await scan.file(photo, kinds: ["qr"]) { out.push(c.value) }
  return out
}
```

## What's in an image: asuka/vision

`vision.analyze(path)` finds what's in an image, on the device: its text,
line by line and word by word, faces, and barcodes, each with its `Box`
(fractions of the image's width and height from its top left, the image
upright: multiply by a view's size for points). Ask only for what you
need (`text:`, `faces:`, `codes:`), and give the text's languages
(`languages: ["ja"]`) when you know them. Nothing leaves the device.

```asuka
import asuka/vision

async fn words(photo: string) throws -> [string] {
  let r = try await vision.analyze(photo, faces: false, codes: false)
  return r.lines.map(l => l.text)
}
```

The Vision framework on iOS and the Mac, ML Kit and its bundled models
on Android (about 30 MB), the browser's Shape Detection API where it has
one, Windows' OCR, and Tesseract (where it's installed) and ZBar on
Linux. `asuka/scan` reads a code's value; this package says where it is.

## The photo library: asuka/photos

`asuka/photos` reads the user's library, with their permission: PhotoKit on iOS
and the Mac, MediaStore on Android, the Pictures folder on Windows. Linux
and the web have no library an app can read: access is `Denied`. It's for an app whose screen is the library. To let the
user pick a few photos, `device.PickPhotos` asks for no permission: prefer it
where it'll do. Google Play asks an app that reads the library why it does, in
the Play Console. Every call waits: not on the main thread.

```go
go func() {
	if a, _ := photos.Request(); a == photos.Denied {
		return
	}
	assets, _ := photos.List(photos.Query{Screenshots: true, Limit: 60})
	thumb, _ := photos.Thumbnail(assets[0].ID, 300) // a file, for ui.Image
	full, _ := photos.Export(assets[0].ID)          // the original, as a file
	fmt.Println(thumb, full.Path, full.Name)
}()
```

- `photos.Current()` says what's allowed without asking: `NotAsked`, `Denied`,
  `Limited` (only the photos the user chose: iOS 14, Android 14) or `Allowed`.
  `photos.Request()` asks the first time. `photos.ManageLimited()` lets a
  `Limited` user change which photos.
- `photos.List(query)` is newest first, from `Offset`, at most `Limit`. An
  `Asset` has its `ID`, `Width`, `Height`, `Created()` and `Screenshot`.
  `Screenshots` keeps only those: iOS knows; on Android it's what's in a
  Screenshots folder.
- `photos.Thumbnail(id, size)` is a JPEG at least `size` pixels on its short
  side, kept in the app's caches for the next time.
- `photos.Export(id)` copies the full image as it is in the library (HEIC,
  JPEG, PNG), with its name. iCloud photos download first.

## Photos, the browser, the clipboard: asuka/device

`asuka/device` is what the platform offers beyond views.

```asuka
import asuka/device

component Gallery {
  state photos: [string] = []

  Column {
    Button("Pick photos") {
      on press { photos = (try? await device.pickPhotos(max: 3)) ?? [] }
    }
    Button("Help") {
      on press { try? await device.browse("https://example.com/help") }
    }
    for p in photos { Image(p) }
  }
}
```

- `device.pickPhotos(max:)` shows the system's photo picker
  (PHPicker on iOS, the Photo Picker on Android; the open dialog on the
  desktops, the file picker in a browser) for up to `max` images
  (1 to 255). It asks for no permission: the user picks, and the app sees
  only what was picked. It returns the images' files, which `Image`
  shows, or none if the user cancelled. The files are copies in the
  app's temporary files or caches: move one to keep it.
- `device.browse(url)` shows a web page in the app (SFSafariViewController,
  a Custom Tab), over the app's screens. The URL must be http or https.
  `openURL` leaves the app for the browser instead. A desktop opens
  the page in the user's browser, and a browser in a new tab.
- `device.clipboardText()` reads the clipboard's text, `none` if it has
  none. iOS asks the user the first time the app reads what another app
  put there. `copy(text)` writes it.
- `device.keepAwake(on)` keeps the screen on while `on` is true: a video,
  a recipe, a map. Turn it off when the screen that wants it goes.
- `device.pickFiles(types:, multiple:)` shows the system's document
  picker (iOS's Files browser, Android's storage picker, the open dialog
  on the desktops, the file picker in a browser; no permission). `types`
  are MIME types (`"application/pdf"`, `"image/*"`; none: any file) and
  `multiple` lets the user pick several. It returns a `device.File` for
  each, its `path` a copy in the app's caches, with its `name`, `type`
  and `size`, or none if the user cancelled.
- `device.clipboardHas()` says whether the clipboard holds `text`, an
  `image` or a `url`, without reading it (iOS doesn't ask the user for
  this): show a Paste button only when there's something to paste.
  `device.clipboardImage()` reads its image into a file, in the format it
  was copied in; `device.copyImage(path)` puts an image file on it.
  `device.clipboardChanges()` is a stream of what it holds at each change
  (iOS and Android say; the Mac and Windows are asked twice a second; a
  browser sees copies in the page, and others' where the user lets it
  read the clipboard).
- `device.info()` is the device and the app: `model` ("iPhone", "Pixel
  9", "Mac", "PC"), `system` ("iOS", "Android", "macOS", "Windows",
  "Linux"), `systemVersion`, and `appVersion` and `build`,
  `asuka.toml`'s `version` and `build`.

```asuka
import asuka/device

component Player {
  let info = load device.info()

  task {
    device.keepAwake(true)
    defer { device.keepAwake(false) }
    await sleep(1h)
  }

  Text(if let i = info.value { "{i.model}, {i.system} {i.systemVersion}" } else { "" })
}
```

## Signing in on the web: asuka/webauth

`webauth.open(url)` shows a sign-in page in the system's own sign-in
sheet: `ASWebAuthenticationSession` on iOS, a Custom Tab on Android.
It's the browser's, so the user's sessions, passkeys and password
autofill work in it, and providers that refuse an app's own `WebView`
(Google's sign-in) don't refuse it. Use it for OAuth rather than a
`WebView`.

The Mac has `ASWebAuthenticationSession` too. Windows and Linux have no
system sheet: the page shows in a web view of the app's over its window
(WebView2, WebKitGTK), which has the app's web profile but not the
user's browser's sessions, and which a provider that refuses embedded
web views refuses there. A browser opens the page in a popup, done when
it comes back to a page of the app's own origin (a custom scheme can't
open a page).

```asuka
import asuka/webauth

async fn signIn(authorizeURL: string) throws -> string? {
  do {
    // myapp://…?code=…: exchange the code.
    return try await webauth.open(authorizeURL, callbackScheme: "myapp")
  } catch webauth.WebAuthError.canceled {
    return none // the user closed it
  }
}
```

It ends in one of two ways:

- **A callback scheme.** When the page goes to a URL of
  `callbackScheme` (the app's own), the sheet closes and `open` returns
  the URL. On Android the scheme must be the app's, asuka.toml's
  `scheme`; it goes to `open` rather than the app's `openURL` event.
- **The app ends it.** Without a scheme, the page redirects somewhere
  the app hears from: an OAuth server that allows a redirect to the
  loopback interface (`http://localhost:port/callback`, RFC 8252) sends
  the code to a server the app runs on that port; racing the two
  (`race(server.next(), webauth.open(url))`) cancels `open`, which
  closes the sheet. On Android, cancelling brings the app back over the
  tab.

`open` throws `webauth.WebAuthError.canceled` when the user closes the
sheet. `ephemeral: true` asks for a private session, without the
browser's cookies (iOS's `prefersEphemeralWebBrowserSession`; Android's
ephemeral Custom Tab where the browser has one; a throwaway profile on
Windows and Linux).

## Copy, share and open: ui.Copy, ui.Share, ui.OpenURL

Three calls in `ui` hand text to the system. All run on the main thread.

```go
ui.Button(ui.ButtonProps{Title: ui.Val("Copy link"), OnPress: func() { ui.Copy("https://example.com/s/42") }}),
ui.Button(ui.ButtonProps{Title: ui.Val("Share"), OnPress: func() { ui.Share("https://example.com/s/42") }}),
ui.Button(ui.ButtonProps{Title: ui.Val("Open in browser"), OnPress: func() { ui.OpenURL("https://example.com") }}),
ui.Button(ui.ButtonProps{Title: ui.Val("Call"), OnPress: func() { ui.OpenURL("tel:+15555550100") }}),
```

- `ui.Copy(text)` puts text on the clipboard.
- `ui.Share(text)` shows the system's share sheet, to send it to an app, a
  contact, AirDrop or Nearby Share. A URL is shared as a link. The Mac
  shows its sharing services picker, Windows its share UI, a browser
  the Web Share sheet where it has one; Linux has none, so the text is
  copied to the clipboard, with a toast (as a browser without one
  copies it).
- `ui.OpenURL(url)` asks the system to open a URL: a web address in the
  browser, another scheme (`tel:`, `mailto:`, another app's) in the app
  that handles it.

`ui.ShareFiles(paths, options, done)` shows the share sheet for files: a
video to send, a PDF to save to Files, photos for AirDrop. `done` runs on
the main thread once the sheet is gone, whether the user shared or not;
its error says when the sheet couldn't show (a file that isn't there).

```go
var share ui.Node
share = ui.Button(ui.ButtonProps{Title: ui.Val("Share"), OnPress: func() {
	ui.ShareFiles([]string{report}, ui.ShareOptions{MIME: "application/pdf", Title: "Send the report", From: share}, func(err error) {
		if err != nil {
			status.Set(err.Error())
		}
	})
}})
```

`ShareOptions` are expo-sharing's:

| Option | |
| --- | --- |
| `MIME` | The files' type, which the apps on the sheet go by. Android sets the intent's type to it. On iOS it's the type when `UTI` is empty. Default: from the first file's extension. |
| `UTI` | iOS: the files' Uniform Type Identifier (`com.adobe.pdf`), ahead of `MIME`. |
| `Title` | The sheet's title: Android's chooser, and a mail's subject on iOS. |
| `From` | The view the sheet points at where it's a popover (iPad, the Mac), usually the button that shares. Default: the screen's middle. |

On iOS a file whose extension doesn't match its type is shared under a
name that does, as expo-sharing does: `image.pdf` shared as a JPEG goes
as `image.pdf.jpeg`, a link to the file, not a copy. On Android the files
go through the app's FileProvider; one outside the app's own folders is
copied to its caches first. On the Mac it's the sharing services
picker. On Windows it's the share UI (`done` is called once it shows:
Windows doesn't say when it's gone).
Linux has none: the files go on the clipboard, as a shared text does. A
browser shares the files with the Web Share sheet where it can, else
downloads each.

To be opened with URLs yourself, see `asuka.toml`'s `scheme` and `links` in
[Shipping](shipping.md#asukatoml), and `ui.OnOpenURL`.

## Previewing files: asuka/preview

`preview.show(sources)` shows files with the system's viewer, as a mail
app shows an attachment: PDFs, images, video, audio, Office and iWork
documents, text. A source is a file's path, an asset's (as `Image`
takes it), or an http(s) URL, which is downloaded to the app's caches
first, under its name (or with the extension of the type the server
gives).

```asuka
import asuka/preview

component Invoice {
  prop path: string
  state status = ""

  Column {
    Button("Open the invoice") {
      on press {
        do {
          try await preview.show([path, "docs/terms.pdf"])
        } catch preview.PreviewError.noViewer {
          status = "Nothing on this device opens it"
        } catch e {
          status = e.message
        }
      }
    }
    Text(status)
  }
}
```

`start:` is the index of the file shown first; `edit: true` lets the
user mark up PDFs and images with Markup, which saves into the files
(iOS). It returns once the viewer is gone, and throws
`preview.PreviewError.noViewer` if nothing can show the files, or another
error (no such file, a download that failed).

| | Viewer | Returns |
| --- | --- | --- |
| iOS | Quick Look (`QLPreviewController`) over the screen: swipe between the files; share, Markup, search | when it's dismissed |
| Android | `ACTION_VIEW` of the `start` file in the app the user picks for its type (Drive's PDF viewer, Photos), through the app's FileProvider with the right to read it alone | when the user is back |
| macOS | Quick Look's panel (`QLPreviewPanel`), its arrows between the files | when the panel closes |
| Windows | the file's default app (`ShellExecuteEx`), or the "How do you want to open this?" dialog | once it's handed over |
| Linux | the file's default app (`GtkFileLauncher`, the portal's in a sandbox) | once it's handed over |
| Web | a new tab, for what a browser shows (images, video, audio, text, PDF, HTML); another type is `noViewer` | once it's opened |

Android has no system viewer, nor one that pages through several files:
it opens one, as Android's mail apps do with each attachment. The view
the viewer grows from on iOS (a `from:`) waits for the prelude's `Ref`.

## Printing and PDFs: asuka/print

`asuka/print` shows the system's print dialog, with its printers and
options (copies, pages, paper, orientation; Save as PDF on Android and the
Mac), for a file or HTML markup; and lays HTML out on pages as a PDF.
Each returns a `print.Outcome` once the dialog is closed. The package's
name is the prelude's `print` too: import it under another name to print
text as well.

```asuka
import asuka/print as printing

async fn printReceipt(receipt: string) throws -> string {
  let r = try await printing.html(receipt, job: "Receipt")
  return if r == .printed { "Sent to the printer" } else { "" }
}
```

- `print.file(source, job:, orientation:)` prints a PDF or an image (a
  path, an asset, an http(s) URL, as `preview.show` takes them). Another
  type throws `print.PrintError.unsupported`, but on Windows and Linux
  (where the printer takes what CUPS can print).
- `print.html(markup, job:, orientation:)` prints markup, paginated as a
  browser prints a page: CSS's `@media print` and `break-before` apply.
  Images and styles come from absolute URLs (`https:`, `data:`).

`job` is the job's name in the queue (default: the file's name, the
page's title, else the app's name), and `orientation` the one the dialog
starts with (`.portrait`, `.landscape`).

An `Outcome` is `.printed` (the job is in the printer's queue, or saved
as a PDF from the dialog), `.cancelled`, or `.unknown` where the platform
doesn't say. A job that fails is an error. `print.PrintError.unavailable`
says the device can't print.

| | Dialog | File | HTML | Outcome |
| --- | --- | --- | --- | --- |
| iOS | `UIPrintInteractionController`, a popover on the iPad | PDFs and images (`printingItem`) | WebKit's print formatter | printed or cancelled |
| Android | `PrintManager`'s print screen | a PDF written through; an image fit to the page, upright (as `PrintHelper`) | the WebView's print adapter | printed, cancelled or failed (the job's state) |
| macOS | `NSPrintOperation`'s panel, a sheet on the window, with a preview | PDFKit (a PDF; an image on a page) | WebKit's print operation | printed or cancelled |
| Windows | WebView2's: the system's print dialog | the file's app prints it (the shell's `print` verb), whatever its type | WebView2 | unknown |
| Linux | GTK's print dialog (the print portal's for a file) | the printer takes it (CUPS: PDFs, images) | WebKitGTK's print operation | printed or cancelled |
| Web | the browser's print dialog | PDFs in the browser's viewer, images | a hidden frame | unknown |

An image wider than tall starts in landscape on Android and iOS, as their
photo printing does. Printing what a `WebView` shows, and the iPad
popover's anchor, wait for the prelude's `Ref`.

### PDFs from HTML

`print.pdf(markup, page:, landscape:, margins:)` lays markup out on
pages, as `print.html` prints it but without a dialog, and writes a PDF
in the app's caches, named after the page's `<title>` ("Receipt.pdf";
"Document.pdf" without one). It returns its path, to share, mail or
move. Its text stays text: searchable, selectable.

```asuka
import asuka/print as printing
import asuka/preview

async fn receiptPDF(receipt: string) throws {
  let path = try await printing.pdf(receipt, page: printing.Page.a4, margins: printing.Margins(top: 48, right: 48, bottom: 48, left: 48))
  try await preview.show([path])
}
```

| Parameter | |
| --- | --- |
| `page` | The paper, in points: `Page.a3`, `a4`, `a5`, `letter`, `legal`, or a size of your own. Default: the user's region's (`print.regionPage(environment().locale)`), Letter in the Americas that use it (CLDR's paper sizes), A4 elsewhere. |
| `landscape` | Turns the page: its width the longer side. |
| `margins` | The space around each page's content, in points. Default: half an inch (36 points) on each side. |

iOS lays it out with WebKit (`UIPrintPageRenderer` with the web view's
print formatter), Android with its WebView's print adapter, the Mac with
WebKit's print operation, Windows with WebView2 (Chromium's
`Page.printToPDF`), Linux with WebKitGTK (printed to a file). A browser
makes PDFs only through its print dialog: there `pdf` throws an
`unsupported` error, and `print.html` lets the user Save as PDF.

## Mail and messages: asuka/compose

`asuka/compose` opens the system's mail and message composers, filled in
for the user to send. Main thread; `done` gets a `compose.Result`:
`compose.Sent`, `Saved` (a draft), `Cancelled`, or `Unknown` where
another app has the mail and doesn't say.

```go
compose.Mail(compose.MailMessage{
	To:      []string{"support@example.com"},
	Subject: "Order 1042",
	Body:    "<p>Hello,</p><p>The receipt is attached.</p>",
	HTML:    true,
	Attachments: []compose.Attachment{{Source: receiptPDF, Name: "Receipt 1042.pdf"}},
}, func(r compose.Result, err error) {
	if errors.Is(err, compose.ErrUnavailable) {
		status.Set("Set up a mail account to send it")
	}
})

compose.Message(compose.TextMessage{To: []string{"+15555550100"}, Body: "On my way"}, nil)
```

A `MailMessage` has `To`, `CC` and `BCC` addresses, a `Subject`, a `Body`
(markup if `HTML`) and `Attachments`; a `TextMessage` has `To` (phone
numbers, or iMessage addresses), a `Subject` (where Messages has subjects
on; elsewhere it goes before the body), a `Body` and `Attachments`. An
`Attachment` is a `Source` (a path, an asset, an http(s) URL), its `Type`
(default: its extension's) and the `Name` the recipient sees (default:
the file's). Where a mail can't carry markup (a `mailto:` link, Simple
MAPI), its body goes as text, as `compose.PlainText` makes it.

`compose.Check()` says what the device can do (not on the main thread):
`Mail`, a mail account or app; `MailComposer`, iOS's own composer, which
says what the user did and takes attachments; `Text`, text messages; and
`TextAttachments`, messages with attachments. `CanSendMail`,
`CanSendText` and `CanSendAttachments` read one each. A call the device
can't do fails with `compose.ErrUnavailable`.

| | Mail | Messages | Result |
| --- | --- | --- | --- |
| iOS | Mail's composer (`MFMailComposeViewController`) over the app; without a Mail account, a `mailto:` link for the mail app the user set (without attachments) | Messages' composer (`MFMessageComposeViewController`) | sent, saved, cancelled; unknown for a link |
| Android | the mail apps only: `ACTION_SENDTO` `mailto:`, or with attachments `ACTION_SEND` with `mailto:` as its selector, the files through the app's FileProvider; markup as spans (`EXTRA_TEXT`) and as `EXTRA_HTML_TEXT` | the default SMS app: `smsto:` with `sms_body`, or with attachments `ACTION_SEND` | unknown, once the user is back |
| macOS | a `mailto:` link for the user's mail app; with attachments, the mail share service (`NSSharingService`), which takes no CC or BCC (they fail with `ErrUnavailable`) | Messages (`NSSharingService`) | unknown, or cancelled |
| Windows | a `mailto:` link; with attachments, Simple MAPI (`MAPISendMailW`: Outlook, Thunderbird; the new Outlook and Windows's Mail app don't take it: `ErrUnavailable`) | `ErrUnavailable` | unknown, or cancelled (MAPI) |
| Linux | the email portal (the mail app's composer, with the attachments), else `xdg-email`, else a `mailto:` link | `ErrUnavailable` | unknown, or cancelled (the portal) |
| Web | a `mailto:` link; with attachments, the Web Share sheet | an `sms:` link; with attachments, the Web Share sheet | unknown |

The iOS simulator has no Mail account and no Messages: try the composers
on a device.

## Location: asuka/location

`asuka/location` is where the device is, now and as it moves, with the
user's permission, which it asks for the first time. It's Core Location
on iOS and the Mac, and Android's location manager, which needs no Google
Play services; WinRT's Geolocator on Windows, the location portal (or
GeoClue) on Linux, and the Geolocation API in a browser. With the app out
of sight too, it follows the device in the background and watches regions
for the app, below.

`location.current()` asks for permission if it hasn't, then returns where
the device is. If the user hasn't allowed it, it throws
`LocationError.denied`.

```asuka
import asuka/location

component Where {
  state where = ""

  Column {
    Button("Where am I?") {
      on press {
        do {
          let p = try await location.current()
          where = "{p.latitude.fixed(4)}, {p.longitude.fixed(4)} (within {p.accuracy.fixed(0)} m)"
        } catch location.LocationError.denied {
          where = "Allow location in Settings"
        } catch e {
          where = e.message
        }
      }
    }
    Text(where)
  }
}
```

`location.positions()` is a stream: the position now, then each as the
device moves. Read it in a task, and it stops with the component:

```asuka
import asuka/location

component Track {
  state here = location.Position()

  task {
    do {
      for await p in location.positions() { here = p }
    } catch e {
      print(e.message)
    }
  }

  Text("{here.latitude.fixed(5)}, {here.longitude.fixed(5)}")
}
```

`location.placeName(latitude, longitude)` is what a place is called
("Mission District, San Francisco"), from the platform's geocoder, which
asks its servers: ask only when the user wants the name. Linux asks
geocode-glib where it's installed; Windows and a browser have none, and
it throws there.

A `location.Position` has `latitude`, `longitude`, `accuracy` (meters:
the radius the device is within), `altitude` (meters above sea level; 0 if
not known), `speed` and `course` (negative when not known) and `time`
(nanoseconds since 1970, as `time.now()`).

### Permission levels

The user allows the location while the app is in use (`.whenInUse`), or
always (`.always`), which the regions need, and the app's events with the
app not running. They also choose precise positions or approximate ones
(within a few kilometers: iOS's Precise Location, Android's
Approximate). `location.authorization()` is what they allowed, tracked:
read in a binding, it follows the changes they make in Settings.

```asuka
import asuka/location

component Allowed {
  let auth = location.authorization()

  Column {
    Text("Allowed: {auth.level}; precise: {auth.precise}{if auth.off { "; location services are off" } else { "" }}")
    Button("Allow always") { on press { let _ = try? await location.requestAlways() } }
    Button("Location settings") { on press { try? await location.openSettings() } }
  }
}
```

- `location.request()` asks for `.whenInUse`, the first time; asking
  again after `.denied` shows nothing: only Settings changes it
  (`location.openSettings()`).
- `location.requestAlways()` asks for `.always` (after `.whenInUse`).
  iOS asks once; Android 11 and later take the user to the app's location
  settings, to choose "Allow all the time".
- `location.requestPrecise(purpose)` asks the user who allowed only
  approximate positions for precise ones: on iOS for now (`purpose` is a
  key of asuka.toml's `ios.locationPurposes`), on Android for good.

A desktop app has `.always` once allowed: it runs out of sight anyway.
Linux and a browser say nothing before the app asks for a position, so
they're `.notAsked` until one comes.

### Updates in the background

`location.startUpdates(…)` starts updates that go on with the app out of
sight, and across launches (on iOS and Android the system starts the app
for them), until `location.stopUpdates()`. `location.updates()` is their
stream, of `Batch`es (`positions`, oldest first): read it as the app
starts, so the batches the system started the app for find a reader.

```asuka
import asuka/location

async fn track() throws {
  try await location.startUpdates(
    accuracy: .high,
    distance: 20,
    activity: .fitness,
    delay: 30s,
    indicator: true,
    notification: location.Notification(title: "Tracking your route", text: "Your run is being recorded"),
  )
}

async fn followUpdates() throws {
  for await b in location.updates() {
    let done = b.hold()   // keeps the app running while it's sent
    defer { done() }
    print("{b.positions.count()} positions")
  }
}
```

`accuracy` trades exactness for power (`.high`, `.navigation`,
`.balanced`, `.low`, `.passive`); `distance` is how far the device moves
between two updates; `interval` how often they come (Android's);
`activity` tunes iOS's updates (and its pausing, with `autoPause`);
`delay` lets updates come together, so the app wakes less; `significant`
gets only the significant changes (about 500 m), at almost no power, and
on iOS they start the app even after the user ended it. `indicator` shows
iOS's blue pill while the app gets updates out of sight. On Android,
`notification` runs a foreground service of the location type, whose
notification it is: with it, updates come at their rate with the app out
of sight. `location.updating()` says whether they run (they may have been
started at an earlier launch).

On a desktop and in a browser, the updates are the positions the app
watches while it runs.

### Regions

`location.monitor(region)` watches a circle for the app, until
`location.stopMonitoring(id)`: its transitions come through
`location.crossings()`, with the app in the background or not running
(iOS and Android start it). If the device is inside as monitoring starts,
an `.enter` comes. It needs `.always` on iOS and Android
(`LocationError.needsAlways`); past the system's limit it throws
`tooManyRegions`.

```asuka
import asuka/location

async fn watchHere() throws {
  let p = try await location.current()
  try await location.monitor(location.Region(id: "here", latitude: p.latitude, longitude: p.longitude, radius: 150, events: [.enter, .exit]))
}

async fn followCrossings() throws {
  for await c in location.crossings() {
    print("{c.kind} {c.region}")
  }
}
```

`location.supported()` says what the system does itself: `regions` (iOS,
Android, macOS and Windows monitor them; on Linux and in a browser this
package computes them from the positions while the app runs),
`maxRegions` (iOS and macOS 20), `dwell` (whether `.dwell` comes) and
`relaunch` (whether the system starts the app for an event).

### Events with the app not running

The system keeps a batch or a crossing until the app has read it: one
that came as the app ended is given again at the next start. An event
keeps the app running a few seconds; `b.hold()` and `c.hold()` keep it
running until the function they return is called (or the system's time
is up: about 30 seconds on iOS, 10 on Android), for work that goes on
after the event is read (sending it to a server).

### Testing

`location.fake()` gives the package a `FakeLocation` instead of the
platform: `answer` is what the user answers when asked, `move(latitude,
longitude)` takes the device somewhere (the positions' readers see it, a
batch comes while updates run, and the regions it enters or leaves cross),
`allow(level)` is the user changing it in Settings, and `pending` counts
the events whose `hold()` isn't done.

```asuka
import asuka/location

test "a region, crossed" {
  let loc = location.fake()
  loc.answer = .always
  loc.move(35.6812, 139.7671)
  let _ = try await location.requestAlways()
  try await location.monitor(location.Region(id: "here", latitude: 35.6812, longitude: 139.7671, radius: 150))
  loc.move(35.70, 139.7671) // 2 km north: an exit
}
```

## Notifications: asuka/notify

`asuka/notify` shows local notifications, now or at a time the app sets,
whether or not the app is running then. Tapping one opens the app with
its URL. It also has what a messaging app needs, for `asuka/push`'s
notifications too: actions and replies typed in the notification,
threads, Android's channels, messages in conversations, the badge, the
notifications showing, and which ones show while the app is open.

```go
go func() {
	ok, err := notify.Request()
	if err != nil || !ok {
		return
	}
	err = notify.Schedule(notify.Notification{
		ID:    "water",
		Title: "Water the plants",
		Body:  "It's been a day.",
		URL:   "myapp://plants",
		After: 24 * time.Hour,
	})
	if err != nil {
		fmt.Println(err)
	}
}()
```

- `notify.Request()` asks the user to allow notifications the first
  time, and reports whether they're allowed: their answer then, or what
  they've set in Settings since. Not on the main thread.
- `notify.Schedule(n)` sets `n` to show `n.After` from now (at least a
  second); `notify.Show(n)` shows it now. One with the `ID` of one
  waiting or showing replaces it. Not on the main thread.
- `notify.Cancel(id)` takes the notification away, waiting or shown. On
  the main thread.

A `Notification`'s `URL` should be one of the app's scheme (`asuka.toml`'s
`scheme`): `ui.OnOpenURL` gets it when the user taps the notification.
Its other fields:

| Field | What it does |
|---|---|
| `Subtitle` | A line under the title (iOS); Android's sub text beside the app's name |
| `Data` | The app's own keys, given back in `Showing`, `OnAction` and `Present` |
| `Category` | A category's ID: the actions it shows (below) |
| `Thread` | Groups it with the thread's others: iOS's thread, an Android group with a summary notification Asuka keeps |
| `Summary` | The line under an Android group's summary ("3 new messages"); iOS writes its own |
| `Channel` | The Android channel it's in (below); iOS has none |
| `Badge` | The app icon's number when it shows (iOS, macOS); Android's notification number |
| `Silent` | No sound |
| `Image` | A picture, as `ui.Image`'s sources (an asset, a file, an http(s) URL): iOS's attachment, Android's big picture |

**iOS** shows a notification while the app is open too, as a banner,
unless `Present` says otherwise. **Android** posts a scheduled one from an
inexact alarm (no exact-alarm permission needed), so it may come a
little after its time.

### Actions and replies

A category is a kind of notification and its buttons. Set the app's
categories as it starts, in its root component, before notifications
that name them come (a push's too: `aps.category` on iOS, the data's
`"category"` on Android). They hold across launches.

```go
notify.SetCategories(notify.Category{ID: "message", Actions: []notify.Action{
	{ID: "reply", Title: "Reply", Icon: "arrowshape.turn.up.left",
		Reply: &notify.Reply{Button: "Send", Placeholder: "Message"}},
	{ID: "read", Title: "Mark as Read"},
	{ID: "delete", Title: "Delete", Destructive: true, Unlock: true},
	{ID: "open", Title: "Open", Open: true},
}})

notify.OnAction(func(r notify.Response) {
	switch r.Action {
	case "": // a tap on the notification
		openChat(r.Notification.Thread)
	case "reply":
		_, done := r.Defer() // sending takes a moment
		go func() {
			defer done()
			api.Send(r.Notification.Thread, r.Reply)
		}()
	case "read":
		notify.RemoveThread(r.Notification.Thread)
	}
})
```

- An `Action` has its `ID` and `Title`, an `Icon` (an SF Symbol; iOS
  15 and macOS 12 show it, Android shows actions without icons),
  `Destructive` (iOS shows it red), `Unlock` (the device unlocks first:
  iOS, Android 12 and later), `Open` (it opens the app) and `Reply` (a
  text field, with its send button's title and placeholder).
- `notify.OnAction(f)` calls `f` on the main thread with each response,
  until `stop`: a tap (`Action` `""`) or an action, `Reply` the text
  typed, `Notification` the one answered (`asuka/push`'s too). A response
  that came before any `f` (the one that launched the app, or woke it)
  waits for the first, so set it as the app starts.
- An action without `Open` runs in the background: the app's Go code
  runs (started without a screen if the app isn't running: `main`, then
  the root component, which must call `OnAction`), and the app doesn't
  come to the front. Work that goes on after `f` returns calls
  `r.Defer()` in `f`, and `done` when it's finished: the system keeps the
  app running until then, or `ctx`'s deadline, a little before its own
  (iOS gives about 30 seconds, Android about 10).

**Android** takes a background action's notification away, as iOS does,
unless the app shows it again (`Show`, with its `ID`) by the time it's
done. After a reply, Android wants the notification posted again (its
spinner turns until then): Asuka adds the reply to a conversation's
notification, under the user's name, and takes another away.

### Threads and the notifications showing

A `Thread` groups a chat's notifications: iOS stacks them, Android puts
them in a group under a summary notification, which Asuka keeps as they
come and go.

- `notify.Showing()` lists the app's notifications that show (in
  Notification Center, the shade), its pushes' too, newest first: each
  `Delivered` has its `ID`, `Title`, `Subtitle`, `Body`, `URL`, `Data`,
  `Category`, `Thread`, `Time`, and `Remote` for a push. Not on the main
  thread.
- `notify.Remove(ids...)`, `notify.RemoveThread(thread)` (a chat that's
  read) and `notify.RemoveAll()` take them away. On the main thread.

### What shows while the app is open

```go
notify.Present(func(n notify.Delivered) notify.Presentation {
	if n.Thread == openChat.Peek() {
		return notify.Hide // the chat is on screen
	}
	return notify.Banner
})
```

`notify.Present(f)` has `f` say how each notification that comes while
the app is in the foreground shows: `Banner` (as with the app closed),
`Quiet` (in Notification Center, the shade, without a banner or sound)
or `Hide`. It runs on the main thread as the notification comes, and
must answer at once: the host waits a second at most, then shows it.
`nil` shows them all again. On the main thread.

### Messages in conversations

A message shows as the platform shows messages, with the sender's
picture: iOS's communication notification, Android's conversation (the
shade's Conversations section, with the conversation's shortcut).

```go
ana := notify.Person{ID: "u7", Name: "Ana", Image: "https://example.com/ana.jpg"}
chat := notify.Conversation{ID: "climbing", Title: "Climbing", Group: true}
go notify.Show(notify.Notification{Body: "Who's in for Saturday?", Category: "message"}.From(ana, chat))
```

- `n.From(sender, c)` makes `n` a message: `sender` said `n.Body` in
  `c`. Its `ID` and `Thread` default to the conversation's `ID` (a 1:1
  conversation's is the sender's), its `Title` to the sender's name (the
  group's title, for a group).
- A `Person` has an `ID` (the same each time), a `Name`, an `Image` (as
  `ui.Image`'s sources) and `Bot`. A `Conversation` has its `ID`, a
  group's `Title` and `Image`, `Group`, `Me` (the user as the
  conversation shows them; Android names their replies with it, "You" by
  default) and `Earlier` messages.
- **Android** shows a conversation as one notification, its messages in
  it: a message adds to the conversation's notification that shows (or
  `Earlier` sets what comes before it). **iOS** shows each message as a
  notification of its own, in the conversation's thread.
- **iOS** needs the Communication Notifications capability on the App
  ID: an app that calls `From` is given its entitlement and Info.plist's
  `NSUserActivityTypes` (`INSendMessageIntent`), and a release build's
  profile must have it. Without it, a message shows as a plain
  notification. **macOS** shows a message as a plain notification.

### Android's channels

A channel is a kind of notification the user can turn off or quiet in
the app's settings. Android shows a notification with its channel's
sound, vibration and importance; iOS has no channels, and ignores them.

```go
notify.SetChannelGroups(notify.ChannelGroup{ID: "chats", Name: "Chats"})
notify.SetChannels(
	notify.Channel{ID: "messages", Name: "Messages", Importance: notify.ImportanceHigh, Group: "chats"},
	notify.Channel{ID: "digest", Name: "Daily digest", Importance: notify.ImportanceLow, NoBadge: true},
)
```

- `notify.SetChannels(cs...)` makes the channels, renames the ones made
  before, and deletes those it made before that aren't in `cs`. A
  channel's `Importance` (`ImportanceDefault`, `ImportanceHigh`: heads-up,
  `ImportanceLow`: no sound, `ImportanceMin`), `Silent`, `NoVibration`
  and `NoBadge` are its when it's made; after, they're the user's to
  change. `notify.SetChannelGroups(gs...)` does the same for groups
  (deleting a group deletes its channels). On the main thread.
- A notification without a `Channel`, or with one that isn't, is in
  the default channel ("Notifications"), which stays. A push names its
  channel with FCM's `android.notification.channel_id`, or a data
  message's `"channel"`.

### The badge

`notify.SetBadge(n)` sets the number on the app's icon; `0` takes it
away. **iOS** and **macOS** (the Dock) show it as set. **Android** has no
badge of its own: launchers that badge count the app's notifications (or
show a dot), so the badge goes as they're read (`Remove`,
`RemoveThread`), and `SetBadge` does nothing; a notification's `Badge`
is the number a launcher may show. A channel with `NoBadge` doesn't
count.

### On each platform

| | iOS | Android | macOS | Windows | Linux | Web |
|---|---|---|---|---|---|---|
| Show, schedule, cancel | ✓ | ✓ | ✓ | ✓ (toasts) | ✓ (scheduled: a systemd user timer, whether the app runs or not) | ✓ (scheduled: while a page of the app is open) |
| Actions, replies | ✓ | ✓ | ✓ | ✓ (five buttons) | actions (a reply opens the app) | buttons where the browser shows them (Chrome, Edge: two); replies where it has inline replies |
| Background actions | ✓ (about 25 s) | ✓ (about 9 s) | ✓ | ✓ (about 25 s; started for it) | while the app runs | in a page of the app (the worker opens one) |
| Threads | ✓ | ✓ (a group and its summary) | ✓ | not grouped; `RemoveThread` | ✓ | not grouped; `RemoveThread` |
| Channels | ignored | ✓ | ignored | ignored | ignored | ignored |
| Conversations | ✓ (communication notifications) | ✓ (MessagingStyle, shortcuts) | plain | plain | plain | plain, the sender's picture its icon |
| Badge | ✓ | the launcher's | ✓ (the Dock) | the taskbar button's overlay | the launcher API's (Dash to Dock, Plasma) | the installed app's (`navigator.setAppBadge`) |
| Showing, removing | ✓ | ✓ | ✓ | ✓ (the notification center's) | what the app showed this run | ✓ (the service worker's) |
| Present | ✓ | ✓ | ✓ | ✓ (not scheduled ones) | ✓ | ✓ |

**macOS** needs the app's bundle, which `asuka dev` and `asuka build` run it
from: an executable run on its own (`go run`) has no bundle ID, and its
calls fail with an error that says so. **Windows** shows toasts, now or scheduled,
with their image, a category's actions as buttons and a reply as a text
box. The app registers itself the first time (a Start menu shortcut with
its AppUserModelID, asuka.toml's `id`, and a COM activator: its executable,
in the user's registry), so that a tap or a button reaches the app,
which Windows starts for it if it isn't running; for a background
action it stays out of sight and quits once `OnAction` is done. The
badge is a count drawn on the app's taskbar button (a packaged app's,
the system's badge). Windows shows a scheduled toast however `Present`
would. **Linux** sends them as GNotifications
when the app has its desktop file (or runs in a sandbox), else to the
desktop's notification server itself (GNOME Shell turns away an app it
has no desktop file for); taps and buttons reach the app while it runs.
A notification due later is a systemd user timer (`systemd-run --user`),
which runs the app's executable without a window to show it, hands it
to the app if it runs, and launches the app for a tap; without systemd
(a Flatpak, `asuka dev`) it shows while the app runs. **A browser** shows
them through the app's service worker, with Web Push for `asuka/push`:
[the web](web.md) has the details.

## Sound: asuka/audio

`audio.play(source)` plays a sound once: a file in the app's assets, a
file's path, or an http(s) URL. `audio.Player(source)` is a store that
holds one sound, ready to play as it's made: `play`, `pause`,
`seek(seconds:)`, `setVolume`, `setRate` (the pitch kept, for speech),
`setLoop`, and `status()`, where it is (`position`, `duration`,
`playing`, `ended`). It stops and lets go of the sound when the
component that made it goes, or at `release()`.

```asuka
import asuka/audio

component Episode(url: string) {
  state player = audio.Player(url)
  state status = audio.Status()

  task {
    while true {
      status = await player.status()
      await sleep(250ms)
    }
  }

  Column {
    Text("{status.position.fixed(0)} / {status.duration.fixed(0)} s")
    Button(if status.playing { "Pause" } else { "Play" }) {
      on press { if status.playing { player.pause() } else { player.play() } }
    }
    Button("Ding") { on press { audio.play("ding.wav") } }
  }
}
```

AVPlayer on iOS and the Mac, Media3's ExoPlayer on Android, an `<audio>`
in a browser, MCI on Windows, GStreamer's `playbin` on Linux. A sound
plays with the screen locked only when the app says it's playing media:
see Now Playing below.

### Now Playing and the media keys: asuka/nowplaying

`nowplaying.NowPlaying` tells the system what the app plays, while it's
in the view: the lock screen's and Control Center's player on iOS, the
media notification on Android (a foreground service, so it keeps
playing), the Now Playing widget on the Mac, the media flyout on
Windows (SMTC), MPRIS on Linux (GNOME's media controls), the Media
Session API in a browser. Its events are the system's commands: the
media keys, the headphones' buttons, the lock screen's. Given a
`player`, it follows its position and plays and pauses it itself.

```asuka
import asuka/audio
import asuka/nowplaying

component Playing(url: string) {
  state player = audio.Player(url)

  Column {
    nowplaying.NowPlaying {
      title: "Episode 12"
      artist: "The Show"
      artwork: "cover.jpg"
      player: player
      skipInterval: 15
      on next { player.seek(0) }
    }
    Button("Play") { on press { player.play() } }
  }
}
```

Without a `player`, give `state` (`.playing`, `.paused`), `duration` and
`position`, and handle `play`, `pause`, `seek(position)`,
`skipForward(seconds)` and `skipBackward(seconds)`. When several are in
the view, the last one shown is what the system shows. An app that
imports it gets iOS's `audio` background mode and Android's
foreground-service permissions.

## Network state: asuka/network

`asuka/network` is the device's network: whether it's online, over Wi-Fi or
cellular, and whether the connection costs. Use it to download large
files on Wi-Fi only, or to say the app is offline. It's NWPathMonitor on
iOS and the Mac, ConnectivityManager on Android, the Network List
Manager on Windows, GNetworkMonitor and NetworkManager on Linux, and
`navigator.onLine` in a browser, which says what carries it only where
it can (Chrome on Android).

`network.Use()` is the network as a signal, for a component: it follows
changes while the component lives.

```go
func Status() ui.Node {
	net := network.Use()
	return ui.Text(signal.Func(func() string {
		n := net.Get()
		if !n.Connected {
			return "Offline"
		}
		if n.Metered || n.Constrained {
			return n.Type.String() + ": large downloads wait for Wi-Fi"
		}
		return n.Type.String()
	}))
}
```

A `network.State` has:

- `Connected`.
- `Type`: `network.None`, `WiFi`, `Cellular`, `Ethernet` or `Other` (a
  VPN alone, Bluetooth...). Its `String` is `"none"`, `"wifi"`,
  `"cellular"`, `"ethernet"` or `"other"`.
- `Metered`: the connection costs or is limited, as mobile data or a
  phone's hotspot.
- `Constrained`: the user asked apps to use less data (iOS's Low Data
  Mode, Android's Data Saver, a browser's Data Saver), or, on Windows,
  the connection is near or over its data limit. Never on Linux.

Until the platform first answers, `Use`'s value is `Connected` with
`Type` `Other`.

`network.Current()` is the network now (not on the main thread), and
`network.Watch(f)` calls `f` on the main thread with the network now and
each time it changes, until `stop` is called.

## Saving to the library: asuka/library

`asuka/library` saves files where the user and other apps find them: a
video to the gallery, a song to the music, anything to Downloads.

```go
library.Save(file, library.Options{Collection: library.Movies, Folder: "My App", Move: true},
	func(where string, err error) {
		if err != nil {
			status.Set("Couldn't save: " + err.Error())
			return
		}
		status.Set("Saved")
	})
```

`library.Save(path, o, done)` copies the file into `o.Collection`, off the
main thread (a large video takes a while), then calls `done` on the main
thread with where it went. It asks for what it needs the first time.
`done` may be nil.

`library.Options`:

- `Collection`: `library.Downloads` (the default), `Movies`, `Music` or
  `Pictures`.
- `Name`: the file's name there (default: its own). A name taken gets a
  number.
- `Folder`: a folder of the collection's for the app's files; empty: the
  collection itself.
- `MIME`: its type (default: from its extension).
- `Move`: remove the file once it's saved, for a download in the app's
  caches: no second copy.

Where each collection goes:

| Collection | Android | iOS | macOS | Windows, Linux |
| --- | --- | --- | --- | --- |
| `Downloads` | Downloads | the app's Documents, in the Files app | `~/Downloads` | the user's Downloads |
| `Movies` | the gallery's videos | Photos | Photos | the user's Videos |
| `Music` | Music | the app's Documents, in the Files app | `~/Music` | the user's Music |
| `Pictures` | the gallery's pictures | Photos | Photos | the user's Pictures |

On Windows and Linux, and in `~/Downloads` and `~/Music` on a Mac, the
file goes under `Folder` in the user's folder, answering with its path;
Linux's folders are XDG's. A browser downloads the file, whatever the
collection: it lands where the browser puts downloads.

**Android** uses the MediaStore collections from Android 10, with no
permission, and answers with a `content://` URI. Before Android 10, it
writes to the shared folders after asking for the storage permission.

**iOS** asks to add to Photos (add only: the app can't read the library)
and answers with the asset's identifier; `Folder` doesn't apply there.
Other collections go to the app's Documents, under `Folder`, answering
with the file's path; Asuka makes Documents visible in the Files app.

## Background work: asuka/background

`asuka/background` keeps the app working while it's out of sight: a
download, an upload, an export the user started, which takes minutes.

`background.Start` starts a task: the app keeps running, with a
notification showing its `Options`, until `End`. `onAction` gets the ID
of a notification button the user taps, on the main thread.
`task.Update` shows new options, its progress as it goes. Start a task
while the app is on screen, as the user asks for the work.

```go
var cancelled atomic.Bool
task := background.Start(background.Options{
	Title:    "Exporting",
	Progress: -1,
	Actions:  []background.Action{{ID: "cancel", Title: "Cancel"}},
}, func(action string) {
	if action == "cancel" {
		cancelled.Store(true)
	}
})
go func() {
	for i := 1; i <= 10 && !cancelled.Load(); i++ {
		time.Sleep(time.Second) // the work
		p := float64(i) / 10
		ui.Post(func() {
			task.Update(background.Options{Title: ui.Val("Exporting"), Text: fmt.Sprintf("%.0f%%", p*100), Progress: p,
				Actions: []background.Action{{ID: "cancel", Title: "Cancel"}}})
		})
	}
	ui.Post(func() {
		task.End()
		if cancelled.Load() {
			status.Set("Cancelled")
		}
	})
}()
```

`background.Options`:

- `Title` and `Text`.
- `Progress`: 0 to 1; negative: under way, how far not known.
- `Actions`: the notification's buttons, each an `ID` and a `Title`.

`Start`, `Update` and `End` run on the main thread; do the work in a
goroutine. Each `Update` replaces all the options, so give the `Actions`
again. [`examples/downloader`](../examples/downloader) downloads in a
task, with Pause, Resume and Cancel.

**Android** stops a backgrounded app within minutes. A task keeps it
running in a foreground service (of the data sync type), with an ongoing
notification for each task, showing its progress and up to three
buttons, and keeps the CPU awake. On Android 13 and later, starting a
task asks the user to allow notifications if they haven't; the work goes
on either way. Android doesn't let an app in the background start one.
Android 15 limits a data sync service to six hours a day. Google Play
asks, in the Play Console, what the app's data sync foreground service is
for: declare it before you upload.

**iOS** gives a backgrounded app about 30 seconds more while a task
lives, and shows no notification: `Update` does nothing there, and
`onAction` is never called. Out of time, iOS ends the task and the work
may stop.

**A desktop** app keeps running out of sight: a task keeps the system
from slowing or suspending it (App Nap on a Mac, sleep on Windows and
Linux), with no notification: `Update` does nothing, and `onAction` is
never called. **A browser** runs the page while it's open, with or
without a task.

## Shares from other apps: ui.OnShare

`ui.OnShare(f)` calls `f` with what another app shares with this one
from its share sheet: a link from YouTube or a browser, a text from
Notes, photos from the gallery, a PDF from Files. It's iOS's and
Android's: on the desktops and in a browser the app isn't offered in
other apps' share menus, and `f` isn't called. `asuka.toml`'s
`shareTarget` says what the app is offered for:

```toml
shareTarget = true
```

takes texts and links. A table takes files too, by MIME type, and at
most `max` at once (default 1):

```toml
[shareTarget]
types = ["text", "image/*", "video/*"]
max = 10
```

A type is `"text"` (texts and web links) or a MIME type: `"image/*"`,
`"video/*"`, `"audio/*"`, `"application/pdf"`, `"*/*"` for any file.

```go
func App() ui.Node {
	link := signal.New("")
	photos := signal.New([]string(nil))
	ui.OnShare(func(items []ui.SharedItem) {
		for _, it := range items {
			switch it.Kind {
			case ui.SharedLink:
				link.Set(it.Text)
			case ui.SharedImage:
				photos.Set(append(photos.Peek(), it.Path))
			}
		}
	})
	return ui.Text(link)
}
```

Each `ui.SharedItem` has a `Kind`: `SharedText`, `SharedLink`,
`SharedImage`, `SharedVideo`, `SharedAudio` or `SharedFile`. A text or a
link is in `Text`. A file is at `Path`, the app's own copy in its caches:
move it where the app keeps it, or remove it when you're done. It comes
with `Name`, its name in the app it came from (`IMG_0042.HEIC`), its
`MIME` type and its `Size` (-1 if not known). The items come in the
order the other app gave them. A page shared from a browser comes as its
title, a text, and its link.

What came before any handler, the share that launched the app, goes to
the first one. The handler stops with the component's owner. To show a
screen of the app's for what's shared, without opening it, see the
share sheet below; to open one in the app, see [Navigation](navigation.md).

**Android** opens the app itself with the share (`ACTION_SEND`, and
`ACTION_SEND_MULTIPLE` when `max` is over 1), for the types' intent
filters. The app copies the files to its caches before `OnShare` gets
them, while its access to them still holds.

**iOS** has a share extension in the app, which opens the app with what's
shared, as Android opens it: the extension shows no screen of its own.
Its activation rule comes from the types: Apple's keys for texts,
links, images, videos and any file, which keep `max`; a predicate on the
attachments' types for others (audio, a PDF), which doesn't. The
extension takes whatever it's given, so check `Kind` and `MIME`. Files
go from the extension to the app through an App Group,
`group.<app ID>`, which a device build's profiles must have (see
[Shipping](shipping.md#ios)).

Under `asuka dev`, the player takes shares of anything, whatever the
app's `shareTarget`. On a phone the files are on the phone, not on the
computer the Go code runs on, so `asuka dev` builds an app whose
`shareTarget` takes files instead of running it in the player there (the
simulator's files are the Mac's).

What a link leads to (a page, an image, a video) is the app's to find
out, with `net/http`: expo-sharing's "resolved" payloads, in Go.

### The app's own share sheet: app.ShareSheet

By default a share opens the app. `app.ShareSheet(screen)` shows the
app's own screen instead, over the app that shares, as "Save to
Raindrop" or "Add to Notes" does: the user saves, and is back where they
were. Call it before `app.Run`.

```go
func ShareSheet(s *ui.ShareSheet) ui.Node {
	note := signal.New("")
	return ui.Column(ui.Style{Grow: 1, Padding: ui.All(16), Gap: 12},
		ui.Text(signal.Static(s.Items[0].Text)),
		ui.TextField(ui.TextFieldProps{Value: note, Placeholder: ui.Val("Note"), Multiline: true}),
		ui.Row(ui.Style{Gap: 12},
			ui.Button(ui.ButtonProps{Title: ui.Val("Cancel"), OnPress: s.Cancel}),
			ui.Button(ui.ButtonProps{Title: ui.Val("Save"), Variant: ui.ButtonFilled, OnPress: func() {
				save(s.Items, note.Peek())
				s.Close()
			}}),
		),
	)
}

func main() {
	app.ShareSheet(ShareSheet)
	app.Run(App)
}
```

`s.Items` is what's shared, as `OnShare` has it. The sheet ends with
`s.Close()` (done), `s.Cancel()`, or `s.OpenApp()`, which opens the app
with the items for its `OnShare`, as without a sheet. Until the items
have come (a video being copied) it shows a spinner.

The sheet is a root of its own, in a process of its own, as iOS runs
share extensions: on **iOS** the app's share extension, which iOS shows
as a sheet, running the app's Go code and the host from the framework it
shares with the app; on
**Android** an Activity in a `:share` process, shown as a Material
bottom sheet over the other app (dragging it down, tapping outside and
back cancel it). The process ends with the sheet: what's pending in
`asuka/store` is written as it closes, and `app.OnLifecycle` hears
`Background`. A file's `Path` is the sheet's until it closes: copy what
the app keeps.

What the app keeps is the same in its sheet: `asuka/store`, SQLite files
in `store.Dir()` (`sqlite.Open("app.db")`), `asuka/secure`. Android's
two processes share the app's files. On iOS the app's data moves to the
App Group (`group.<app ID>`), once, the first time it runs with a sheet,
and its keychain group is shared with the extension. The app doesn't
hear what its sheet wrote while it was running: look again when it
comes back (`app.OnLifecycle`, `db.Changed()`).

The CLI sees that the app's Go code calls `app.ShareSheet` and builds
the sheet instead of the extension that opens the app. On iOS the app's
Go code and the host are then a framework in the app,
`Frameworks/AsukaApp.framework`, which the app and its extension both
load, as Xcode shares code with an extension: the app isn't bigger for
its sheet. Under `asuka dev` the player's sheet runs the app's sheet from
the Mac too. [`examples/bookmarks`](../examples/bookmarks) is a
sheet as Raindrop's: the page's title, description and image, a note, a
collection and tags.

## JavaScript: asuka/js

`asuka/js` runs JavaScript off screen, in the platform's own engine: a web
view that's never shown (WKWebView on iOS and the Mac, Android's WebView,
WebView2 on Windows, WebKitGTK on Linux), or in a browser a hidden,
sandboxed frame. It's for code
that only has a JavaScript form, as a site's player whose functions sign
its video URLs.

`js.eval(script)` runs a script, statements as a `<script>`'s, in a
fresh page of its own, and returns its value as JSON: the last
statement's value, or what the promise it returns resolves to (`null` for
`undefined`). A thrown error, or a rejected promise, is the error, with
the script's message. The page is gone after: what one script sets isn't
there for the next. It waits without holding the main thread.

```asuka
import asuka/js
import asuka/json

async fn doubled() throws -> [int] {
  let v = try await js.eval("const xs = [1, 2, 3].map(x => x * 2); xs")   // "[2,4,6]"
  return try json.decode<[int]>(v)
}

async fn later() throws -> string {
  let v = try await js.eval("new Promise(done => setTimeout(() => done('later'), 100))", timeout: 10s)
  return try json.decode<string>(v)
}
```

A script may run for its `timeout` (a minute by default); one that runs
out throws `js.JsError.timeout`. Cancelling the task (the screen that
started it going) lets its page go.

For script in a page the user sees, use `WebView`
([Web pages](components.md#web-pages)).

## Push notifications: asuka/push

`push.Register()` asks the user to allow notifications (the first time),
registers the device with Apple's (APNs) or Google's (Firebase Cloud
Messaging) service, and reports its `Token`, its `Service` (`"apns"`,
`"fcm"`) and whether notifications are `Allowed`. Send the token to your
server, which sends notifications to it; register at each launch, since
the token can change. Not on the main thread. A Mac registers with APNs
as iOS does (with a provisioning profile: see [On a Mac](#on-a-mac)). On
the web it's Web Push (`"webpush"`, the token the browser's
subscription; see [the web](web.md)). Windows and Linux have no push
service for an app: `Register` fails there.

```go
func App() ui.Node {
	push.OnOpen(func(m push.Message) { openThread(m.Data["thread"]) })
	go func() {
		if r, err := push.Register(); err == nil {
			api.SaveDeviceToken(r.Service, r.Token)
		}
	}()
	return home()
}
```

- `push.OnReceive(f)` gets each message that comes while the app runs.
  It's shown as a notification too, in the foreground as well, if it has
  a title or body. Data messages (silent pushes) come here only.
- `push.OnOpen(f)` gets each notification the user taps, including the
  one that launched the app, if `f` is set as the app starts. A
  message's data `"url"` also goes to `ui.OnOpenURL`, as
  `asuka/notify`'s do.
- A `push.Message` has its `ID`, `Title`, `Body` and `Data` (iOS: the
  payload's keys beside `aps`; Android: the message's data).

iOS needs the Push Notifications capability on the App ID for a release
build; the simulator gets a token too. Android needs a Firebase project:
add the Android app to it, download its `google-services.json`, and name
it in `asuka.toml`:

```toml
[android]
googleServices = "google-services.json"
```

Asuka writes its settings into the app as Google's Gradle plugin would.
Without it, `Register` fails with what to do.

### Categories, threads and channels in a push

A push uses `asuka/notify`'s categories (its actions, `notify.OnAction`),
threads, channels, badge and `Present`, named in its payload:

- **iOS**: `aps`'s `category`, `thread-id` and `badge`, as APNs has them.
- **Android**: a notification message's `android.notification.channel_id`
  and `image`; a data message's `"title"`, `"body"`, `"subtitle"`,
  `"category"`, `"thread"`, `"summary"`, `"channel"`, `"image"`, `"badge"`
  and `"url"`. A data message with a `"title"` or `"body"` shows as a
  notification (Asuka builds it, with its actions); one without is
  silent (`OnReceive` only). A notification message that comes while
  the app is in the background is shown by Firebase, without actions or
  a thread: send data messages for them.

A `push.Message` has these too: `Subtitle`, `Category`, `Thread`,
`Channel`, `Image`, `Badge`, `Summary`.

### Rewriting a push before it shows

`push.Rewrite(f)` has the app's Go code change each push before it
shows: decrypt an end-to-end encrypted payload, set its title and body,
attach an image, make it a message in a conversation with the sender's
picture, set its thread and category. Call it in `main`, before
`app.Run`.

```go
func main() {
	push.Rewrite(func(m *push.Message) error {
		text, from, err := decrypt(m.Data["box"]) // with a key in asuka/secure
		if err != nil {
			return err // shows the push as it came
		}
		m.Body = text
		m.Category = "message"
		m.From(notify.Person{ID: from.ID, Name: from.Name, Image: from.Avatar},
			notify.Conversation{ID: m.Data["chat"]})
		return nil
	})
	app.Run(App)
}
```

`f` runs on its own goroutine, not the main thread. An error, or a
panic, shows the push as it came. `m.Image` is downloaded and attached;
`m.From` makes it a message, as `notify.Notification`'s `From` does.

**iOS** rewrites in a Notification Service Extension, which `asuka build`
makes for an app that calls `push.Rewrite`: it runs the app's Go code in
a process of its own for each push with `"mutable-content": 1` in its
`aps` (and an `alert`, which shows if the rewrite fails). `main` runs,
then `f`; the root component doesn't, so keep `main` light. iOS gives it
about 30 seconds and 24 MB, and shows the push as it came when time runs
out. What the app keeps is the same there: its data (`asuka/store`, files
in `store.Dir`) is in the App Group the two share, its keychain items
(`asuka/secure`) in the app's group. A release build needs a provisioning
profile for the extension's bundle ID, the app's plus `.notification`
(`asuka.toml`'s `ios.notificationProfile` names one; by default, the best
match installed), with the App Groups capability and the app's group.
A rewrite can't hide a push on iOS.

**Android** rewrites FCM's data messages in the app's process: the Go
code starts without a screen if the app isn't running, rewrites within
about ten seconds, and the notification shows. A data message whose
rewrite leaves no title or body doesn't show.

The player in `asuka dev` has no extension (the app's Go code runs on
the computer), so `asuka dev` builds an app that calls `push.Rewrite`
instead of running it in the player, and the build has it. The simulator runs the extension only for pushes that
come from APNs, not for `xcrun simctl push`.

## Sign in with Apple: asuka/appleauth

`appleauth.Button` is Apple's own button (its title, logo and colors, in
the user's language), and `appleauth.SignIn` shows the system's sheet.
The App Store requires Sign in with Apple of an app that offers another
company's sign-in for its accounts.

```go
appleauth.Button(appleauth.ButtonProps{
	Type:  appleauth.ContinueButton,
	Style: appleauth.Automatic, // black in light mode, white in dark
	OnPress: func() {
		go func() {
			c, err := appleauth.SignIn(appleauth.Options{Scopes: appleauth.FullName | appleauth.Email, Nonce: nonce})
			if err == nil {
				api.SignInWithApple(c.IdentityToken, c.AuthorizationCode)
			}
		}()
	},
})
```

The `Credential`'s `User` is the account's stable ID; send its
`IdentityToken` (a JWT Apple signs) and `AuthorizationCode` to your
server, which checks them with Apple. `Email`, `GivenName` and
`FamilyName` come only the first time the user signs in to the app: keep
them then. `ErrCanceled` is the user closing the sheet.
`appleauth.CredentialState(user)` says whether the user still uses Sign
in with Apple for the app (`Revoked`: sign them out). On Android there's
none: `Button` is nothing, `Available` is false and `SignIn` fails with
`ErrNotAvailable`; so on Windows, Linux and the web. Sign in with Apple
there on the web (`asuka/webauth`). The Mac has it as iOS does, with a
provisioning profile (see [On a Mac](#on-a-mac)).

## Wallet payments: asuka/pay

`asuka/pay` takes payments with the platform's wallet: Apple Pay on
iOS, macOS and in Safari, Google Pay on Android and in the other
browsers. It's for physical goods and services (an order, a ride, a
ticket, a donation); digital content sold in the app is the stores'
in-app purchases. The user pays in the wallet's sheet, and the app gets
the wallet's payment token for its processor (Stripe, Adyen,
Braintree...), which its server charges. Asuka never charges a card.

```go
pay.Button(pay.ButtonProps{
	Type:  pay.Buy,
	Style: pay.Automatic, // black in light mode, white in dark
	OnPress: func() {
		go func() {
			p, err := pay.Pay(pay.Request{
				Country: "US", Currency: "USD",
				Items: []pay.Item{{Label: "Tea set", Amount: "40.00"}, {Label: "Tax", Amount: "3.20", Kind: pay.Tax}},
				Total: pay.Item{Label: "Asuka Tea", Amount: "43.20"},
				Shipping: pay.Name | pay.Address | pay.Email,
				Apple:    pay.Apple{MerchantID: "merchant.com.example.tea"},
				Google:   pay.Google{Gateway: "stripe", GatewayParams: map[string]string{"stripe:version": "2018-10-31", "stripe:publishableKey": key}},
				OnShippingContact: func(c pay.Contact) (pay.Update, error) {
					return api.Quote(c.Address) // the items and total for the address
				},
				OnAuthorize: func(p pay.Payment) error {
					return api.Charge(p.Token) // nil: paid; an error shows in the sheet
				},
			})
			if err == nil {
				ui.Post(func() { orderPlaced.Set(p.Shipping.Name) })
			}
		}()
	},
})
```

`pay.Available(request)` says what the wallet can do for the request's
cards: its `Wallet`, `CanPay` (show the button) and `HasCard` (the user
has one it takes). Show `Button` only where `CanPay` holds; it's the
wallet's own (`PKPaymentButton`, Google's `PayButton`, Safari's and
Google's on the web), whose brand guidelines ask that apps don't draw
their own: its words (`Buy`, `Checkout`, `Book`, `Donate`, `Order`,
`Subscribe`, `Plain`, and Apple's others) and logo are in the user's
language, and its `Style` is `Black` on light backgrounds, `White` on
dark ones, `WhiteOutline` on white, or `Automatic`. Its height is the
wallet's (44 points on iOS, 30 on the Mac, Google's 48); give it the
width. Where Apple Pay has no card yet, Apple's guidelines show the
`SetUpButton`, which calls `pay.SetUp` to add one (or the Buy button,
whose sheet adds one too); Google Pay's sheet adds a card itself.

`pay.Pay(request)` shows the sheet and returns the `Payment` once it
closes, or `ErrCanceled`; it isn't on the main thread. Amounts are
decimal strings in the currency's major unit (`"12.75"`, at most two
decimals for Google Pay; negative for a discount). The `Total`'s label
is the business's name, as the card statement shows it ("Pay Asuka Tea"
in Apple Pay's sheet); the `Items` are the lines above it, their `Kind`
for Google Pay's sheet. The request also says the cards taken
(`Networks`, `Cards`, `CardCountries`), what to ask for with the
shipping and billing contacts (`Name`, `Email`, `Phone`, `Address`),
the `ShippingType` and `ShippingMethods` (the first chosen), where
Google Pay ships (`ShippingCountries`), and Apple Pay's coupon codes
(`Coupons`). `Recurring` makes it a subscription's payment, with its
regular billing and a trial; `Deferred`, one charged later (a hotel);
`AutomaticReload`, a stored balance's top up. The wallet shows the
charges to come, and its token is a merchant token for them.

The handlers run on `Pay`'s goroutine while the sheet waits (about 30
seconds at most), so they may call the app's server:
`OnShippingContact` as the user picks an address (the wallets give only
its city, state, postal code and country before the payment: enough for
the tax and the shipping methods), `OnShippingMethod`,
`OnPaymentMethod` (a card's surcharge) and `OnCoupon`. Each answers an
`Update` (new items, total or methods; what's nil stays) and an error,
which the sheet shows by its field: `pay.Invalid(pay.ShippingPostalCode,
"...")`, `Unserviceable` for an address the app doesn't ship to,
`CouponInvalid`, several in `errors.Join`. Write their messages for the
user. `OnAuthorize` gets the `Payment` as the user pays (Face ID, Touch
ID, a PIN): send its `Token` to the server, which charges it, and
answer with its error. The sheet shows it, and the user may try again;
`nil` closes the sheet with a check mark, and `Pay` returns the payment
with its card, contacts and shipping method.

The token is what the processor's SDKs send. Apple Pay's is the
`PKPaymentToken`'s `paymentData`, a JSON object (Stripe takes it as
`pk_token`, Braintree and Adyen as the Apple Pay token), signed for the
merchant ID's payment processing certificate, which the processor holds.
Google Pay's is tokenized by the request's `Google.Gateway`, with the
gateway's parameters (Stripe's publishable key, Adyen's or Braintree's
merchant account), or encrypted for the app's server with `DirectKey`.

Apple Pay needs a merchant ID (developer.apple.com, Identifiers, Merchant
IDs) with the processor's payment processing certificate, in
`asuka.toml`:

```toml
[applePay]
merchantIds = ["merchant.com.example.tea"]
```

An app that imports the package gets the in-app-payments entitlement
with them; its App ID needs the Apple Pay capability with the same IDs,
for the profile of a release build (a Mac app needs one too: see
[On a Mac](#on-a-mac)). The simulator's sheet pays with its test cards,
which return a token no processor charges. On the web, Apple Pay needs
the site's domain registered for the merchant ID, and
`OnValidateMerchant`: the app's server asks Apple for a merchant session
with the merchant identity certificate. Google Pay's `TEST` environment
needs no account and returns test tokens; `Google.Production` needs the
merchant ID of the Google Pay & Wallet Console, and the app approved
there. An Android app that imports the package gets the Google Pay
library (`play-services-wallet`), its API enabled in the manifest and
the service its sheet calls back; others go without.

On iOS and the Mac it's PassKit's `PKPaymentAuthorizationController`
and `PKPaymentButton`. On Android it's the Google Pay API: `isReadyToPay`,
`loadPaymentData` through the Activity Result API, its dynamic price
updates and authorization through its callbacks service. In a browser,
Apple Pay where the browser has it (Safari, and an iPhone's or iPad's
browsers) through the Payment Request API's Apple Pay method; Google Pay
elsewhere, through Google's own web API (`pay.js`, loaded from
pay.google.com), which has the same request and callbacks as Android in
every browser. `Pay` is called from the button's tap: the browsers show
a sheet only in answer to one. Windows and Linux have no wallet for
apps: `Available` says none, `Pay` fails with `ErrNotAvailable` and
`Button` is nothing.

Tests play the user with `host/fake`'s `Wallet`: it shows what the app
asked for, changes the address, the method or the coupon, and pays
(Apple Pay's events, or Google Pay's on Android and the web):

```go
var w *fake.Wallet
a := uitest.Mount(t, Checkout, uitest.Platform(ui.PlatformIOS), uitest.Setup(func(h *fake.Host) { w = h.Wallet() }))
a.Tap("Buy with Apple Pay")
a.Until("the sheet", func() bool { return w.Sheet != nil })
w.ChangeShippingContact(fake.PayContact{State: "CA", PostalCode: "95014", CountryCode: "US"})
a.Until("the tax", func() bool { return w.Sheet.Total.Amount == "43.20" })
w.Authorize(fake.PayAuthorization{Token: "tok", Card: fake.PayCard{Network: "visa", Kind: "credit", Name: "Visa 1234"}})
a.WaitFor("Thanks!")
```

[examples/checkout](../examples/checkout) sells a tea set this way.

## Face ID and fingerprints: asuka/biometric

`biometric.authenticate(reason:)` shows the system's prompt (Face ID,
Touch ID, Android's BiometricPrompt; Touch ID on a Mac, Windows Hello on
Windows, a passkey's WebAuthn prompt in a browser) and returns if it's
the device's owner, or throws `BiometricError.canceled`, `.notAvailable`
(no biometry and no passcode, or locked out) or `.failed`. It waits
without holding the main thread.

```asuka
import asuka/biometric

component Lock {
  state unlocked = false

  Button(if unlocked { "Unlocked" } else { "Unlock" }) {
    on press {
      do {
        try await biometric.authenticate(reason: "Unlock your notes")
        unlocked = true
      } catch biometric.BiometricError.canceled {
      } catch e {
        unlocked = false
      }
    }
  }
}
```

- `reason` says why (iOS's subtitle; Android's title); `cancel:` is the
  cancel button's title.
- The user can use the device's passcode, PIN or pattern instead, unless
  `noPasscode: true`: biometry only (on Android, a strong one, Class 3).
- `biometric.check()` says what the device has: its `kind` (`.face`,
  `.fingerprint`, `.iris`, `.optic`), whether one is `enrolled`, and
  whether it has a `passcode`.

Linux has no system prompt for the owner: `check` says there's nothing,
and `authenticate` throws `.notAvailable`.

## Recording sound: asuka/record

`record.audio()` records the microphone until `record.stopAudio()` (or
`maxDuration`) and gives the file: AAC in an MPEG-4 file (.m4a) in the
app's caches (or at `path`), which `asuka/audio` plays. It asks for the
microphone the first time. `pauseAudio` and `resumeAudio` pause and go
on; `audioStatus()` is where it is (`level`, from 0 to 1, for a meter;
`elapsed`). `quality: .music` keeps more than a voice needs.

```asuka
import asuka/record

component Memo {
  state recording = false
  state file = ""

  Button(if recording { "Stop" } else { "Record" }) {
    on press {
      if recording {
        record.stopAudio()
      } else {
        recording = true
        defer { recording = false }
        file = try await record.audio(maxDuration: 60s)
      }
    }
  }
}
```

`background: true` keeps it recording out of sight: on iOS the app needs
`"audio"` in asuka.toml's `ios.backgroundModes`; on Android it's a
foreground service whose notification says `title`, with a button to
stop. AVAudioRecorder on iOS and the Mac, `MediaRecorder` on Android and
in a browser, Media Foundation on Windows, GStreamer on Linux.

## Speech to text: asuka/transcribe

`transcribe.file(path)` turns a recording into text, on the device:
nothing leaves it. Its `Result` has the `text`, its `segments` (each
word's or phrase's start and duration, in seconds, where the platform
gives them) and the `language` it heard. `transcribe.check(language)`
says whether the device can, before you record.

```asuka
import asuka/transcribe

async fn caption(recording: string) throws -> string {
  let can = try await transcribe.check("en-US")
  if !can.onDevice { return "" }
  let r = try await transcribe.file(recording, language: "en-US")
  return r.text
}
```

The Speech framework on iOS and the Mac, asked to stay on the device;
Android's on-device recognizer (Android 13 and later); Windows' speech
recognition. Where the device can't (a browser, Linux, an older
Android), it throws `TranscribeError.notAvailable` rather than send the
sound to a server; `.downloading` while the language's model comes.

## The screen: asuka/screen

```asuka
import asuka/screen

component Player {
  state note = ""

  task {
    screen.lock(.landscape)               // a video player
    screen.setBars(style: .light, hideStatus: true)
    screen.preventCapture(true)           // a ticket's code
    defer {
      screen.lock(.any)
      screen.setBars()
      screen.preventCapture(false)
    }
    for await _ in screen.screenshots() { note = "That screenshot shows your balance" }
  }

  Text(note)
}
```

- `screen.lock(o)` turns the app to `o` and keeps it there: `.portrait`,
  `.portraitUp`, `.landscape`, `.landscapeLeft`, `.landscapeRight`;
  `.any` unlocks it. Only the orientations `asuka.toml` allows. iPads in
  Split View or Stage Manager, and Android 16's large screens, ignore it.
- `screen.setBars(style:, hideStatus:, hideNavigation:)` sets the status
  bar's style (`.auto` follows dark mode; `.light` is light icons, for a
  dark picture under them; `.dark`, dark ones), and hides the status bar
  or Android's navigation bar (a swipe from the edge shows them a
  moment). The safe area follows.
- `screen.preventCapture(on)` keeps the app out of screenshots and
  recordings. Android makes the window secure: captures are black, and
  the recent apps show it blank. iOS can't stop a screenshot: it covers
  the app while the screen is recorded, mirrored or on AirPlay.
- `screen.screenshots()` is a stream of the screenshots the user takes
  (iOS; Android 14 and later).

A desktop window has no orientation or system bars: `lock` and
`setBars` do nothing there. `preventCapture` keeps the app's windows out
of screenshots and recordings on the Mac and Windows (not on Linux); a
desktop doesn't tell the app of its screenshots. A browser locks the
orientation only full screen or in an installed app, hides the bars by
going full screen, and has neither.

These are the app's, not a screen's: a screen that sets one sets it back
as it goes (a `defer` in its task).

## The launch screen: asuka/splash

`asuka.toml`'s `splash` is the launch screen: an image (a square PNG, a
logo), `size` points wide (default 120, at most 160), in the middle of a
color, in light and dark mode.

```toml
[splash]
image = "logo.png"
size = 96
background = "#4A63B5"
darkBackground = "#1B2440"
```

It shows from the tap on the icon until the app's first frame: iOS's
launch screen, then a copy of it over the app until the first frame is
ready; Android 12's splash screen (the image in the middle of the icon's
circle) and, before, the window's background. The desktops and the web
have no launch screen: there `hold` and `hide` do nothing. To keep it up
while the app loads, call `splash.hold()` as it starts (in the root
component) and `splash.hide()` once it's ready:

```asuka
import asuka/splash

component Root {
  state ready = false

  task {
    splash.hold()
    await loadSession()
    ready = true
    splash.hide()
  }

  if ready { Home() }
}

async fn loadSession() { await sleep(300ms) } // the app's own: what its first screen needs
component Home { Text("Signed in") }
```

## Maps: asuka/maps

`maps.View` is a map: Apple Maps on iOS and the Mac, Google Maps on
Android, OpenStreetMap's tiles on Windows and in a browser, and
libshumate's on Linux. Bind its `region` (a center and how many degrees
it spans) to move it and follow the user's moves; `markers` are its
pins, and `markerPress`, `press` and `regionChange` what the user does.

```asuka
import asuka/maps

component Places {
  state region = maps.Region(center: maps.Coordinate(lat: 37.77, lng: -122.42), latDelta: 0.08, lngDelta: 0.08)
  state picked = ""

  Column {
    maps.View {
      bind region: region
      markers: [maps.Marker(id: "hq", at: maps.Coordinate(lat: 37.79, lng: -122.39), title: "HQ", color: .blue)]
      mapType: .muted
      grow: 1
      on markerPress(id) { picked = id }
    }
    Text(picked)
  }
}
```

`mapType` is `.standard`, `.satellite`, `.hybrid` or `.muted`, and
`showsUser` shows the user's location (ask for it first, with
`asuka/location`). OpenStreetMap's maps show streets (in a browser,
`.muted` in quieter colors), and the user's location only in a browser. On Android, an app that imports the package is built with
Google's Maps SDK and needs a Maps SDK for Android key in asuka.toml's
`android.mapsKey`.

## Background refresh: asuka/periodic

`asuka/periodic` runs the app's Go code now and then while it isn't on
screen: a sync, a fetch of what's new. `Handle` sets a task's code, in
the root component; `Schedule` asks the system to run it.

```go
func App() ui.Node {
	periodic.Handle("sync", func(ctx context.Context) error {
		return syncInbox(ctx)
	})
	periodic.Schedule("sync", periodic.Options{Every: time.Hour, Network: true})
	return home()
}
```

The system decides when, from `Every` (15 minutes at least), the
battery, the network, and how the user uses the app; it may wait much
longer, and the user can turn it off (iOS's Background App Refresh).
`Network` runs it only online and `Charging` only while charging. The
`ctx` ends before the system's deadline: about 25 seconds on iOS (a
processing task, for `Charging`, has minutes), 9 minutes on Android
(WorkManager). An error asks Android to retry it.

When the system runs a task with the app closed, the app's Go code
starts without a screen: `main` runs, and the root component (which
calls `Handle`), then the task. If the user opens the app then, its
screen appears. `Cancel(name)` stops a task.

A desktop doesn't launch an app for its tasks: on macOS
(`NSBackgroundActivityScheduler`), Windows and Linux they run on a timer
while the app runs, and one that wants power waits while on the battery.
A browser runs no page's tasks while it's closed: `Schedule` fails there.

## The stores: asuka/review, asuka/tracking, asuka/integrity, asuka/agerange

`review.request()` asks the store for its rating prompt (the App
Store's, Google Play's in-app review). The stores decide whether it
shows, on their own quotas, and don't say; ask after a success, never
from a button. For a "Rate us" button, `openURL(review.appStoreURL(id))`
or `review.playStoreURL(id)`.

`tracking.request()` shows iOS's App Tracking Transparency prompt, which
an app must before it reads the advertising identifier or shares data
with ad networks, and returns `.allowed`, `.denied`, `.restricted` or
`.notAsked`; `tracking.current()` reads it without asking. Android has
no such prompt: it's `.allowed`.

`asuka/integrity` proves to your server that a request comes from your
app on a real device. iOS: App Attest. `integrity.generateKey()` makes a
key (keep its ID), `integrity.attestKey(id, challenge)` has Apple attest
it for your server's challenge, once, and `integrity.assertion(id,
data)` signs each request after. Android: Play Integrity.
`integrity.prepare(cloudProjectNumber:)` at launch, then
`integrity.token(requestHash:)` for each request. The server checks what
they return with Apple or Google. `integrity.supported()` is false on the
iOS simulator.

`agerange.request(13, 18)` asks for the user's age range without their
birth date, for the laws that ask apps to know a child: Apple's Declared
Age Range (iOS 26) and Play's Age Signals. The platform may ask the user
or their parent to share. The `Range` has a `status`, `.shared` with a
`lower` and `upper` age (0: open), `.declined`, or
`.verificationRequired` (Play); its `source` says how it's known.
`AgeRangeError.notAvailable` where the platform can't answer.

```asuka
import asuka/agerange
import asuka/review
import asuka/tracking

async fn afterFirstOrder() throws {
  try? await review.request()
  if try await tracking.request() == .allowed { print("personalized ads") }
  let r = try await agerange.request(13, 18)
  if r.status == .shared && r.lower >= 18 { print("an adult") }
}
```

Off the phones: the Mac has the App Store's rating prompt, the tracking
prompt, and App Attest (with a provisioning profile: see
[On a Mac](#on-a-mac)), but not Declared Age Range. Windows, Linux and
the web have no store prompt (`review.request` does nothing), no
attestation (`integrity.supported` is false) and no age range
(`.notAvailable`). Tracking needs no prompt there: Windows answers
`.allowed` while the advertising ID is on in Settings, Linux `.allowed`,
a browser `.denied` when it sends Global Privacy Control or Do Not Track.

Selling in the app, `asuka/purchase`, has a page of its own:
[In-app purchases](purchases.md). So does iCloud's key-value store and
documents, `asuka/icloud`: [iCloud](icloud.md).

## Motion and sensors: asuka/sensors

`asuka/sensors` reads the device's motion and environment sensors, each
a stream that runs while it's read:

```asuka
import asuka/sensors

component Level {
  state roll = 0.0

  task {
    for await m in sensors.motion(every: 50ms) { roll = m.attitude.roll }
  }

  Text("{(roll * 180 / 3.14159).fixed(0)}°")
}
```

- `sensors.accelerometer(every:)`, `gyroscope(every:)` and
  `magnetometer(every:)` give a `Vector` (`x`, `y`, `z`, `time`) about
  every interval: acceleration in g, rotation in radians per second, the
  magnetic field in microteslas (calibrated).
- `sensors.motion(every:)` is the fused device motion: `gravity` and the
  user's `acceleration` (in g), `rotation`, and the `attitude`: a `matrix`
  that turns device axes into East-North-Up, the `heading` (degrees from
  magnetic north; -1 without a magnetometer), and `pitch` and `roll`.
- `sensors.pressure(every:)` is the barometer (hectopascals, and the
  change in altitude), `sensors.light(every:)` the ambient light in lux
  (Android, Linux), and `sensors.stepUpdates()` the steps since watching
  began. `sensors.steps(from:, to:)` (times as `time.now()` gives them) is
  iOS's history (a week); elsewhere it throws `SensorError.notAvailable`.
- `sensors.available(sensor)` says whether the device has one
  (`Sensor.all` lists them); the simulator and emulators have few. A Mac
  and a PC have none; Linux has the light sensor and the accelerometer's
  orientation on convertibles and tablets (iio-sensor-proxy); a browser
  has the Generic Sensor API or device motion events, without a barometer
  or pedometer. A stream of a sensor the device lacks throws
  `notAvailable`.
- `sensors.simulate(json)` feeds the iOS simulator, which has no motion
  sensors, a reading in Core Motion's terms (`kind` 0 the accelerometer,
  3 motion; `acceleration`, `gravity`, `user`, `rotation`, `field`).

The values are the same everywhere: device axes as Android and the W3C
define them (x to the right of the screen, y up it, z out of it),
acceleration as the accelerometer measures it (a device lying face up
reads z = +1 g; iOS's are turned to match), and the attitude computed the
same way on every platform. Up to 200 readings a second; they come on the
main thread.

In a test, `sensors.fake()` gives the package a `FakeSensors`: the
sensors `present`, and readings sent through it (`accelerate(x, y, z)`,
`rotate`, `move(gravity, user:, rotation:, field:)`, `press(hPa)`,
`shine(lux)`, `walk(steps)`), each to the streams that read the sensor,
no more often than they asked.

```asuka
import asuka/sensors

test "lying face up" {
  let s = sensors.fake()
  s.accelerate(0, 0, 1)
}
```

## Editing images and video frames: asuka/imageedit

`imageedit.edit(source, actions)` edits an image with the platform's own
codecs and writes it as JPEG or PNG: what the camera and the photo
picker give (HEIC, JPEG, PNG, WebP...), turned upright as their
orientation says, for an upload, a cache or an `Image`. The actions run
in order: `.resize(width, height)` (a 0 keeps the aspect),
`.crop(x, y, width, height)`, `.rotate(degrees)`, `.flipHorizontal`,
`.flipVertical`, `.fill(...)` (a solid color: what was there is gone)
and `.pixelate(...)`. `imageedit.size(source)` reads an image's size;
`imageedit.videoFrame(source, at:)` takes a video's frame as an image.

```asuka
import asuka/imageedit

async fn thumbnail(photo: string) throws -> string {
  let r = try await imageedit.edit(photo, [.resize(1080, 0), .crop(0, 0, 1080, 1080)], quality: 0.8)
  return r.path
}

async fn poster(video: string) throws -> string {
  return (try await imageedit.videoFrame(video, at: 1s, actions: [.resize(640, 0)])).path
}
```

Core Graphics and Image I/O on iOS and the Mac, `Bitmap` and
`MediaMetadataRetriever` on Android, a canvas in a browser, WIC and
Media Foundation on Windows, GdkPixbuf and GStreamer on Linux. A source
that isn't an image throws `EditError.noImage`.

## Text to speech: asuka/speech

`speech.speak(text)` speaks, in the device's voice for the text's
language (or `Options(language:, voice:, rate:, pitch:, volume:)`), and
returns when it's done; what's spoken while it speaks waits its turn.
`stop()` stops (the speaking calls throw `SpeechError.stopped`),
`pause()` and `resume()` pause at a word and go on, `speaking()` says
whether it is, and `voices()` lists the voices, with their `quality`.

```asuka
import asuka/speech

component Reader(text: string) {
  Row {
    gap: 12
    Button("Read") { on press { try await speech.speak(text, options: speech.Options(rate: 1.1)) } }
    Button("Stop") { on press { speech.stop() } }
  }
}
```

AVSpeechSynthesizer on iOS and the Mac, Android's `TextToSpeech`, the
browser's `speechSynthesis`, SAPI on Windows, Speech Dispatcher on
Linux. Other apps' sound ducks while it speaks.

## Contacts: asuka/contacts

- `contacts.Pick(done)` shows the system's contact picker: the user picks
  one, and the app sees only that one, with no permission asked (on
  Android, without the permission, its name only).
- `contacts.Add(contact, done)` shows the system's new contact screen,
  filled in, where the user saves it; no permission asked.
- `contacts.Request()` asks for access, and `contacts.List(query)` reads
  the address book (by name, up to a limit): names, organization, phones
  and emails with their labels, birthdays. iOS 18's limited access lists
  the contacts the user chose.

On a Mac it's the Contacts framework: `Pick` is the system's picker, a
popover from the window's top, and `Add` asks in an alert over the
window (macOS has no new contact screen), then saves. Linux reads and
writes Evolution Data Server's address books, as [On Linux](#on-linux)
says. Windows gives the contacts to packaged apps only: `Request` says
denied, `Pick` fails, and `Add` opens a vCard in the user's contacts
app. A browser picks one where it has the Contact Picker (Chrome on
Android), and `Add` downloads a vCard.

## Calendars: asuka/calendar

- `calendar.Add(event, done)` shows the system's event editor, filled in,
  where the user saves the event; no permission asked (iOS 17 and later).
  iOS says whether it was saved; Android's calendar apps don't.
- `calendar.Request()` asks for full access; then `calendar.Calendars()`
  lists the calendars and `calendar.Events(from, to, ids...)` the events
  between two times, each occurrence of a repeating one.
- An all-day event's `Start` and `End` are days: midnight UTC of the
  first, and of the day after the last.

On a Mac it's EventKit: `Add` asks in an alert over the window (macOS
has no event editor for apps), then saves. Linux reads and writes
Evolution Data Server's calendars. Windows gives the calendars to
packaged apps only: `Request` says denied, and `Add` opens an iCalendar
file in the user's calendar app (`saved` is false: it doesn't say). A
browser can't read calendars; `Add` downloads an `.ics` file.

## Home screen widgets: asuka/widget

`asuka.toml`'s `widgets` declares the app's widgets; `widget.Set` fills
them, from the app's Go code:

```toml
[[widgets]]
kind = "today"
name = "Today"
description = "What's next"
sizes = ["small", "medium", "rectangular"]
```

```go
widget.Set("today", widget.Entry{
	Title: "Next", Symbol: "calendar", Value: "3 tasks", Body: "Call Ana at 4",
	Progress: 0.4, ShowProgress: true, Until: meeting, URL: "myapp://today",
})
```

A widget shows its `Entry` with the platform's template, laid out for
its size: the `Title` (with its `Symbol`, in the `Tint`), a large `Value`,
the `Body`, a `Progress` bar, a live countdown to `Until`, a `Caption`.
It keeps showing it until the app sets another, whether or not the app
runs; tapping it opens the app with its `URL` (`ui.OnOpenURL`). Several
entries, each with its `At`, are a timeline: each shows from its time.
`Clear` empties it.

Sizes: `small`, `medium`, `large`, and iOS's `extraLarge` (iPad) and
lock screen `circular`, `rectangular` and `inline`. iOS: a WidgetKit
extension the CLI builds into the app, sharing an App Group with it
(a release needs the App Groups capability, and a profile for the
extension's ID, the app's plus `.widgets`). Android: an App Widget for
each kind, Material You colors, sized to the launcher's cells. The
desktops and the web have none: `widget.Set` does nothing there.

## Live activities: asuka/liveactivity

A live activity shows what's happening now outside the app, as it
changes: a delivery on its way, a ride, a game's score, a timer. On iOS
it's a Live Activity (iOS 16.2): the lock screen, the Dynamic Island, a
banner on an iPhone without one. On Android it's an ongoing
notification, which Android 16 promotes to a Live Update: at the top of
the shade and the lock screen, with a chip in the status bar.

```go
a, err := liveactivity.Start(liveactivity.Content{
	Title: "Order #1042", Subtitle: "Ana is picking it up", Value: "12 min",
	Symbol: "bag.fill", Until: eta, URL: "myapp://orders/1042",
}, liveactivity.Options{})

// Later, on the main thread:
a.Update(liveactivity.Content{Title: "Order #1042", Subtitle: "2 stops away", Value: "4 min", Until: eta},
	liveactivity.Alert{Title: "Almost there", Body: "Ana is 2 stops away"})
a.End(liveactivity.Content{Title: "Order #1042", Subtitle: "Delivered"}, liveactivity.Dismiss{})
```

An activity shows its `Content` with the platform's template, which
leaves out what's empty: the `Title` and `Subtitle`, the `Value` (large
at the side; the Dynamic Island's compact view; Android 16's status bar
chip), the `Symbol` in the `Tint`, a `Progress` bar, and a countdown to
`Until` that runs without the app. With `Since`, `Until` and
`ShowProgress`, the bar fills by itself. Tapping it opens the app with
its `URL` (`ui.OnOpenURL`).

`Start` runs while the app is on screen (neither platform lets an app in
the background start one) and isn't on the main thread; `Update` and
`End` are. An `Alert` lights the screen up with the update: iOS expands
the Dynamic Island and shows it on an Apple Watch; Android alerts with
the notification again. `End` leaves the last content showing until the
system takes it (4 hours at most), until `Dismiss.At`, or takes it at
once with `Dismiss.Now`. `Running` lists the activities still running
(after a relaunch, for instance), with what each shows; `Enabled`
reports whether the user allows them (Android asks for notifications at
the first `Start`).

`Observe` says what happens to them: one `Ended` (by the user, the
system, a server or `End`), one `Started` by a server, and the push
tokens. With `Options.Push`, a server updates and ends an activity while
the app is away: send its `PushToken` and its `ID` to the server. On iOS
it sends an APNs push of the `liveactivity` type, whose `content-state`
is the `Content` as JSON (its `MarshalJSON`, from Go); on Android, a
Firebase data message with `asuka.liveActivity` (`update`, `end`),
`asuka.id` and `asuka.content`. A server starts one with iOS 17.2's
`PushToStartToken` (the attributes type `AsukaActivityAttributes`, with
the new activity's `id`), or on Android with `asuka.liveActivity` set to
`start`. The package's doc has the payloads. Pushing needs
`asuka/push` in the app: the Push Notifications capability on iOS,
Firebase on Android.

The iOS build adds the widget extension for an app that calls the
package, with the activities' template (SwiftUI's, as the widgets'), and
`NSSupportsLiveActivities`; a release needs a profile for the
extension's ID, as widgets do. The user can turn Live Activities off for
the app in Settings, and Android 16's promotion in its notification
settings. The desktops and the web have none: `Enabled` is false and
`Start` fails with `ErrNotAvailable`.

## Shortcuts: Siri, Spotlight, the launcher

`asuka.toml`'s `shortcuts` are actions the system offers for the app.
Each opens the app with its URL, for `ui.OnOpenURL`:

```toml
[[shortcuts]]
id = "newNote"
title = "New Note"
url = "notes://new"
symbol = "square.and.pencil"
phrases = ["New note in ${app}"]
```

On iOS they're App Shortcuts: Siri hears their phrases (each with
`${app}`, the app's name), and they're in Spotlight, the Shortcuts app
and on the Action button, with no setup by the user (up to ten). The CLI
compiles them as App Intents and extracts their metadata as Xcode does.
On Android they're the launcher's shortcuts, shown on a long press of
the app's icon, with the symbol in the accent color. The desktops and
the web don't add them.

Simulator builds are signed as Xcode signs them, with the keychain's
Apple Development identity (`asuka.toml`'s team's): App Intents only run
in an app with a team. Without one, the build is signed ad hoc, and the
CLI says the shortcuts won't run.

## Haptics: ui.Haptic

`ui.Haptic(kind)` plays haptic feedback (UIFeedbackGenerator;
`performHapticFeedback` on Android): `ui.HapticSelection`,
`HapticLight`, `HapticMedium`, `HapticHeavy`, `HapticSuccess`,
`HapticWarning` or `HapticError`. A Mac plays the trackpad's feedback
(a Force Touch trackpad: an alignment for `HapticSelection`, a level
change for the rest); a browser vibrates where it can (Chrome on
Android); Windows and Linux have no haptics, and do nothing.

```go
ui.Haptic(ui.HapticSuccess)
```

See [Motion](motion.md) for feedback that goes with animation.

## Crash reports: asuka/crash

An app that imports `asuka/crash` keeps a report of each crash, and reads
it on its next launch, to send where it likes: its own server, Sentry,
Crashlytics. Asuka uploads nothing.

```go
func App() ui.Node {
	crash.OnReport(func(r crash.Report) {
		go func() {
			if err := send(r); err == nil { // your upload: r is JSON
				crash.Delete(r.ID)
			}
		}()
	})
	crash.Set("user", userID)
	// ...
}

// Elsewhere, as the app goes:
crash.Breadcrumb("opened the cart")
crash.Record(err) // an error handled, reported too
```

`OnReport(f)` calls `f` on the main thread with each report kept, then
with each that comes (a diagnostic iOS delivers a moment after launch,
an error recorded), once each, until its `stop`. `Reports()` reads them
all, oldest first (not on the main thread). A report stays until
`Delete(id)`; past `MaxReports` (50) the oldest go. Its `ID` is the same
each time it's read, so a server can tell one sent twice.

What's reported, as a `Kind`, on each platform:

| | iOS | Android | macOS | Windows | Linux | Web |
|---|---|---|---|---|---|---|
| `Go`: a panic not recovered, on any goroutine; a fatal error (a concurrent map write, a deadlock, out of memory) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ (the host keeps the runtime's output) |
| `Exception`: uncaught | NSException (Objective-C, Swift's `NSException`s) | a JVM `Throwable` | — | — | — | an uncaught JavaScript error (not fatal) |
| `Native`: a signal, a Mach or Windows exception | MetricKit | the system's tombstone | MetricKit | the runtime's report | the runtime's report (a signal in native code) | — |
| `Hang` | MetricKit's hangs (not fatal), the watchdog | ANRs, with their traces | MetricKit | — | — | — |
| `OutOfMemory` | the memory limit, when MetricKit says | low-memory kills | — | — | — | — |
| `Resource` | CPU and disk-write exceptions (not fatal) | excessive resource use | MetricKit | — | — | — |
| `NonFatal`: `Record(err)` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |

A Go crash is kept by Go's runtime itself (`runtime/debug.SetCrashOutput`,
set as the app starts, before its `main`), with every goroutine's stack.
An uncaught exception is written by the host's handler as the app ends
(a handler set before, a crash SDK's, still runs after). The rest the
system hands over on a later launch: iOS and macOS's MetricKit
diagnostics, and Android's record of how the app's processes ended
(`getHistoricalProcessExitReasons`, Android 11 and later; a native
crash's tombstone from Android 12). A crash the system reports again
after the host's handler did (iOS's SIGABRT of an uncaught exception,
Android's `REASON_CRASH`) is left out.

Each report has its `Context`: the app's version and build, the device's
model and system, the process and how long it had run, whether it was in
the foreground, and the last `Breadcrumb`s (`MaxBreadcrumbs`, 100) and
the values `Set`. They're written to the app's data directory as they
change, a line appended each (no sync, so as cheap as a write), the file
rewritten when it's twice what it needs: they're on disk before the
crash, whatever kills the app. The lifecycle's changes are breadcrumbs
too ("app: background").

A report's `Text` is what the platform gives (Go's crash output, a stack
trace, a tombstone, an ANR's traces); its `Threads` are those stacks
read into frames, the crashed one first. A Go frame has its function,
file and line. A native one has its binary, the binary's UUID (Mach-O)
or build ID (ELF), the address and its offset in the binary: what a
service symbolicates with the build's symbols (see
[Shipping](shipping.md#symbols-for-crash-reports)).

**Sending to Sentry or Crashlytics.** For Sentry, make an event of each
report (its envelope endpoint, `https://<host>/api/<project>/envelope/`
with your DSN's key): `exception.values[0]` with the `Kind` as its
`type`, the `Message` as its `value`, and the crashed thread's frames as
its `stacktrace.frames`, outermost first (Sentry's order, the reverse of
a `Thread`'s), each with `function`, `filename` and `lineno`, or
`instruction_addr` and `package` for a native frame; and in
`debug_meta.images` each binary of the frames, `{"type": "macho",
"debug_id": BinaryID, "image_addr": Address-Offset}` (`"elf"` on
Android), so the dSYMs or symbols you upload with `sentry-cli` apply.
The `Context` goes in `contexts`, `tags` and `breadcrumbs`. Crashlytics
has no public endpoint to send to: send the reports to your server, or
record them in Crashlytics' SDK as non-fatal errors from your own native
module ([Native code](native.md)). Either way, App Store review asks
about crash data: if you send it, say so (`ios.collected` in
`asuka.toml`, `{type = "CrashData", purposes = ["AppFunctionality"]}`).

**On each platform.** iOS links MetricKit only into an app that reads
its reports (`crash.Reports` or `crash.OnReport`), and delivers its
diagnostics to the app, not its share extension. An app with its own
share sheet (`app.ShareSheet`) keeps the sheet's crashes with its own,
in their App Group, and either reads them. MetricKit has a crash's process ID from iOS 17 on, which ties it
to its run's context; before, it's the run that was last before it. It
doesn't deliver on the simulator. Android needs nothing more; before
Android 11 there's no record of native crashes and ANRs. macOS: MetricKit
from macOS 12; an uncaught Objective-C exception that ends the app is
MetricKit's crash (AppKit catches those of the main thread and logs
them). Windows: Go's runtime handles every exception the process
doesn't (its last handler): it prints the exception and the goroutines,
and exits. Without a console (a release, built for the GUI), Asuka sends
what the runtime prints to a file of the run's, which the next launch
reads into a `Native` report. Asuka sets no unhandled exception filter of
its own: Go's handler runs first, and replacing it would break Go's.
Windows Error Reporting keeps its record as ever. In a browser, Go's
runtime can't write its crash output to a file (`SetCrashOutput` needs
`dup`), so the host keeps what the runtime prints while Go runs and, as
the program ends with a panic or a fatal error, writes it to the run's
record, in the app's files (IndexedDB): the next launch reads it. An
uncaught JavaScript error, the host's or the app's `web/*.js`, is an
`Exception` report at once, not fatal (the page goes on), with the run's
context; at most 20 a run, each once. A page marks its run alive in the
origin's `localStorage`, so the app's other open pages leave its record
alone.

**Under `asuka dev`** the Go code runs on your Mac (or PC): its crashes
print in the terminal, as ever, and are kept there, so `crash.Reports`
on the next run has them. The player's own native crashes aren't the
app's, and aren't reported to it; a build of the app (`asuka dev --once`,
`asuka build`) reports everything.
