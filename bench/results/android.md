## Android

sdk_gphone16k_arm64 emulator, 2026-11-16; release builds; the median of 3 runs (after one to warm up).

Memory is the proportional set size (PSS); its peak, the resident set's high-water mark (VmHWM).

| scenario | | asuka | Compose |
|---|---|---:|---:|
| **startup** | from the process's start to the first frame, ms | 465 | 293 |
| **memory-idle** | at the first frame, MB | 46.9 | 34.9 |
|  | peak since launch, MB | 132 | 118 |
| **mount** | 1,000 rows appear: the main thread's time, ms | 427 | 377 |
| **memory-mount** | with the 1,000 rows, MB | 81.5 | 69.2 |
|  | peak since launch, MB | 168 | 154 |
| **update-one** | mean ms a frame | 5.35 | 35.6 |
|  | 95th percentile | 34.1 | 70.4 |
|  | dropped frames | 40.0 | 229 |
|  | frame's whole time, ms (with the GPU) | 72.3 | 87.1 |
| **update-all** | mean ms a frame | 30.7 | 50.9 |
|  | 95th percentile | 63.8 | 67.4 |
|  | dropped frames | 104 | 120 |
|  | frame's whole time, ms (with the GPU) | 79.6 | 106 |
| **scroll** | mean ms a frame | 31.3 | 30.7 |
|  | 95th percentile | 62.2 | 60.5 |
|  | dropped frames | 287 | 276 |
|  | frame's whole time, ms (with the GPU) | 81.9 | 80.2 |
| **memory-list** | after scrolling the list, MB | 82.5 | 67.6 |
|  | peak since launch, MB | 177 | 195 |
