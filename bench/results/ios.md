## iOS

iPhone 17 Pro simulator, 2026-11-16; release builds; the median of 3 runs (after one to warm up).

Memory is the footprint iOS counts against the app (phys_footprint).

| scenario | | asuka | SwiftUI |
|---|---|---:|---:|
| **startup** | from the process's start to the first frame, ms | 3467 | 3266 |
| **memory-idle** | at the first frame, MB | 17.1 | 18.7 |
|  | peak since launch, MB | 17.2 | 18.9 |
| **mount** | 1,000 rows appear: the main thread's time, ms | 171 | 361 |
| **memory-mount** | with the 1,000 rows, MB | 36.0 | 99.1 |
|  | peak since launch, MB | 36.6 | 104 |
| **update-one** | mean ms a frame | 1.48 | 6.60 |
|  | 95th percentile | 1.87 | 6.88 |
|  | dropped frames | 0.00 | 1.00 |
| **update-all** | mean ms a frame | 13.9 | 126 |
|  | 95th percentile | 16.1 | 136 |
|  | dropped frames | 6.00 | 120 |
| **scroll** | mean ms a frame | 1.36 | 1.20 |
|  | 95th percentile | 6.08 | 6.92 |
|  | dropped frames | 3.00 | 0.00 |
| **memory-list** | after scrolling the list, MB | 33.0 | 34.6 |
|  | peak since launch, MB | 49.6 | 151 |
