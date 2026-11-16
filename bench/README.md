# Benchmarks: Asuka against SwiftUI and Jetpack Compose

The same app, three times: `asuka/` (Asuka, on iOS and Android), `swiftui/`
(SwiftUI) and `compose/` (Jetpack Compose), each written the way its
framework's own documentation would have it. Each runs one schedule of
scenarios, frame by frame, and a meter reports what each costs.

```sh
go run ./bench/run -ios               # asuka and SwiftUI, on a simulator of the benchmarks' own
go run ./bench/run -android           # asuka and Compose, on the running emulator or device
go run ./bench/run -ios -android -runs 7
```

The runner builds release builds of each, installs them, runs each
`-runs` times (interleaved, after one run of each to warm up), and
writes the medians to `results/ios.md` and `results/android.md` (and
every run's numbers to the `.json` beside them).

## The scenarios

At set frames after launch (the same frame numbers in every app):

| scenario | what happens | what's reported |
|---|---|---|
| startup | the app launches and shows its first screen | ms from the process's start to the second frame |
| memory-idle | the first screen | memory, and its peak since launch |
| mount | 1,000 rows (a label and a number) appear in a scroll view (not lazy: `ScrollView`/`VStack`, `Column`/`verticalScroll`, `Scroll`) | the main thread's time over the frames it takes |
| memory-mount | with the 1,000 rows | memory, and its peak |
| update-one | one row's number changes, a different row each frame, for 240 frames | the main thread's time a frame: mean, 95th percentile; dropped frames |
| update-all | every row's number changes each frame, for 120 frames (they all show one counter) | the same |
| scroll | a lazy list of 10,000 two-line rows (`List`, `LazyColumn`, `List`) scrolls 8 points a frame, for 300 frames | the same |
| memory-list | after the scroll | memory, and its peak |

State is each framework's idiomatic fine-grained kind: a store's state
per row (Asuka), an `@Observable` per row (SwiftUI's Observation), a
`mutableIntStateOf` per row (Compose); the shared counter is read by
each row, so a change to it updates the rows, not the screen.

## The meter

The same file in every app on a platform (`asuka/meter/swift/Meter.swift`,
`asuka/meter/android/src/main/kotlin/dev/asuka/app/Meter.kt`, which the
Compose app links to; the Asuka app calls them through its native
package, `meter/`, a `native interface` in Asuka):

- **iOS**: a frame's cost is the main thread's busy time between two
  vsyncs: the run loop's time awake, from a `CFRunLoopObserver`'s
  `AfterWaiting` to its `BeforeWaiting`. That holds the framework's
  update, layout and Core Animation's commit (drawing layers' contents
  included). The render server's work isn't counted. A dropped frame is
  a vsync the display link missed. Memory is `phys_footprint`, what iOS
  counts against an app and kills it for, and its peak since launch.
- **Android**: the same: the main thread's busy time between two
  vsyncs, from the looper's message logging, which holds the
  Choreographer's frame (input, animation, where Asuka's frame events and
  Compose's recomposition run, measure and layout, recording the
  drawing) and whatever else runs; a frame that draws nothing (an update
  out of sight) counts too. `FrameMetrics` give each drawn frame's whole
  time (with the render thread and the GPU) and whether it missed its
  deadline. Memory is the proportional set size, and the resident set's
  high-water mark (`VmHWM`) for the peak.

Asuka and SwiftUI are driven by a display link (Asuka's `onFrame`),
Compose by `withFrameNanos`; each changes its state at the start of the
frame, as an animation would.

## Fairness

- Release builds: Swift `-O`; the Asuka app compiled to C with the core
  (`-O2`); R8 on Android, not
  debuggable. Both Android apps are compiled ahead of time in full
  (`cmd package compile -m speed`) after installing, so neither runs on
  the interpreter or depends on a profile (Compose ships baseline
  profiles; Asuka's host doesn't yet).
- iOS runs on a simulator only the benchmarks use (`asuka-bench`): UI
  automation turns accessibility on, and with it UIKit does a great deal
  of work for every label it draws (on the simulator the device panel
  used during development had, one frame of `update-all` took 24 s, in
  `AXUIContextDrawingAnnotation`). On Android, the runner warns if an
  accessibility service is on.
- A simulator and an emulator are not devices: the numbers compare the
  frameworks with each other on the same machine; they don't say how
  fast either is on a phone. Emulators' GPUs especially are slow, which
  the frames' whole times and dropped frames on Android mostly show. Run
  on devices for numbers to quote.

## Results

The latest are in `results/`. iOS, on the C core (an iPhone 17 Pro
simulator, iOS 27, an Apple M5, 2026-11-16; the median of 3 runs). Main
thread ms; memory MB, its peak in parentheses.

| | Asuka | SwiftUI |
|---|---:|---:|
| mount, 1,000 rows | 171 | 361 |
| update-one, a frame | 1.48 | 6.60 |
| update-all, a frame | 13.9 | 126 |
| scroll, a frame | 1.36 | 1.20 |
| memory, idle | 17.1 | 18.7 |
| memory, the rows | 36.0 (36.6) | 99.1 (104) |
| memory, after the list | 33.0 (49.6) | 34.6 (151) |

Startup isn't comparable on this machine yet: both apps take over 3 s
from the process's start on this simulator runtime, most of it the
runtime's, not the apps'.

Android, on the C core (a Pixel emulator, Android 17, the same Mac,
2026-11-16; the median of 3 runs, with other builds running on the
machine, so read Asuka against Compose of the same run):

| | Asuka | Compose |
|---|---:|---:|
| startup | 465 | 293 |
| mount, 1,000 rows | 427 | 377 |
| update-one, a frame | 5.35 | 35.6 |
| update-all, a frame | 30.7 | 50.9 |
| scroll, a frame | 31.3 | 30.7 |
| memory, idle | 46.9 | 34.9 |
| memory, the rows | 81.5 (168) | 69.2 (154) |
| memory, after the list | 82.5 (177) | 67.6 (195) |

Where Asuka trails on Android: startup and idle memory, which the C
core (no Go runtime) should win back; they're being looked into.
