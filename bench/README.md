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
simulator, iOS 27, an Apple M5, 2026-11-17; the median of 3 runs). Main
thread ms; memory MB, its peak in parentheses.

| | Asuka | SwiftUI |
|---|---:|---:|
| mount, 1,000 rows | 88.8 | 174 |
| update-one, a frame | 1.84 | 4.61 |
| update-all, a frame | 6.21 | 58.0 |
| scroll, a frame | 1.73 | 1.72 |
| memory, idle | 16.6 | 18.5 |
| memory, the rows | 37.1 (38.0) | 101 (107) |
| memory, after the list | 28.4 (45.5) | 33.7 (160) |

Startup isn't comparable on this machine yet: both apps take over a
second from the process's start on this simulator runtime, most of it
the runtime's, not the apps'.

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

# Benchmarks: the VM

`vm/` measures the bytecode VM (what `asuka run`, `asuka test` and the
players run) on one program, `vm/programs/bench`: the cross-language
suite (`fib`, `sieve`, `nbody`, `trees`, `words`, `json`, `sort`), at
sizes an interpreter finishes in about a second, and what an
interpreter's own costs show in (`loop`: int arithmetic and a counted
loop; `closures`: a closure made and called each turn; `iface`:
interface calls; `strings`: concatenation, interpolation, comparison).
The program is compiled once; what's timed is asuka-run loading and
running it.

```sh
go run ./bench/vm                      # each benchmark, hyperfine's table (-runs, -warmup, -json file)
go run ./bench/vm -instr -runs 3       # instructions and cycles retired (median): what a busy machine doesn't change
go run ./bench/vm -ref                 # with CPython, LuaJIT's interpreter (luajit -joff) and Bun on vm/ref's programs
go run ./bench/vm -noopt               # compiled without the optimizer (ASUKA_OPT=0)
go run ./bench/vm -c                   # the program compiled to C, as asuka build makes it
go run ./bench/vm -only fib,loop -disasm loop   # a function's bytecode
go run ./bench/vm -emit /tmp/b         # the module and the runner's command lines, for a profiler
```

`vm/ref` has the interpreter-bound benchmarks in Python, Lua and
JavaScript, the same algorithms at the same sizes (they print what
Asuka's do): reference points, an interpreter's (CPython, LuaJIT's
interpreter) and a JIT's (Bun's JavaScriptCore). `words`, `json`,
`sort` and `strings` measure libraries (maps, the JSON codec, sorting,
strings) more than the VM and have none.

On a busy machine read `-instr`'s instructions: wall time and cycles
move with the load, instructions retired don't (macOS's `time -l`,
Linux's `perf stat`). `results/vm.md` has the last run.
