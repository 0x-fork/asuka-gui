## iOS

iPhone 17 Pro simulator, 2026-11-17; release builds; the median of 3 runs (after one to warm up).

Memory is the footprint iOS counts against the app (phys_footprint).

| scenario | | asuka | SwiftUI |
|---|---|---:|---:|
| **startup** | from the process's start to the first frame, ms | 1394 | 1332 |
| **memory-idle** | at the first frame, MB | 16.6 | 18.5 |
|  | peak since launch, MB | 16.7 | 18.5 |
| **mount** | 1,000 rows appear: the main thread's time, ms | 88.8 | 174 |
| **memory-mount** | with the 1,000 rows, MB | 37.1 | 101 |
|  | peak since launch, MB | 38.0 | 107 |
| **update-one** | mean ms a frame | 1.84 | 4.61 |
|  | 95th percentile | 3.20 | 5.64 |
|  | dropped frames | 0.00 | 1.00 |
| **update-all** | mean ms a frame | 6.21 | 58.0 |
|  | 95th percentile | 7.62 | 66.1 |
|  | dropped frames | 1.00 | 120 |
| **scroll** | mean ms a frame | 1.73 | 1.72 |
|  | 95th percentile | 7.16 | 11.8 |
|  | dropped frames | 6.00 | 0.00 |
| **memory-list** | after scrolling the list, MB | 28.4 | 33.7 |
|  | peak since launch, MB | 45.5 | 160 |
