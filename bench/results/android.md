## Android

sdk_gphone16k_arm64 emulator, 2026-11-17; release builds; the median of 3 runs (after one to warm up).

Memory is the proportional set size (PSS); its peak, the resident set's high-water mark (VmHWM).

| scenario | | asuka | Compose |
|---|---|---:|---:|
| **startup** | from the process's start to the first frame, ms | 81.0 | 87.0 |
| **memory-idle** | at the first frame, MB | 41.8 | 34.4 |
|  | peak since launch, MB | 129 | 119 |
| **mount** | 1,000 rows appear: the main thread's time, ms | 110 | 86.3 |
| **memory-mount** | with the 1,000 rows, MB | 64.6 | 68.8 |
|  | peak since launch, MB | 153 | 155 |
| **update-one** | mean ms a frame | 0.72 | 14.2 |
|  | 95th percentile | 1.35 | 30.8 |
|  | dropped frames | 3.00 | 30.0 |
|  | frame's whole time, ms (with the GPU) | 22.5 | 41.4 |
| **update-all** | mean ms a frame | 5.78 | 29.4 |
|  | 95th percentile | 11.3 | 38.3 |
|  | dropped frames | 3.00 | 86.0 |
|  | frame's whole time, ms (with the GPU) | 28.0 | 62.5 |
| **scroll** | mean ms a frame | 11.2 | 14.9 |
|  | 95th percentile | 28.1 | 29.6 |
|  | dropped frames | 24.0 | 35.0 |
|  | frame's whole time, ms (with the GPU) | 40.7 | 47.4 |
| **memory-list** | after scrolling the list, MB | 57.7 | 64.1 |
|  | peak since launch, MB | 169 | 181 |
