# The VM's benchmarks: results

`go run ./bench/vm -instr` (and `-c`, `-noopt`, `-ref`) on an Apple M5, macOS,
2026-11-19, with other agents' builds and tests running (load averages
of 80 to 600 on 10 cores): so instructions retired, which the load
doesn't change, in billions; cycles in parentheses move with it. The
median of 3 runs for the VM, 1 for the C and the references. Before is
f9bf57d2 (the switch interpreter, no optimizer); "VM" the interpreter's
changes alone (`-noopt`); "VM + optimizer" what `asuka run` runs.

## The VM

| benchmark | before | VM | VM + optimizer | × |
|---|---:|---:|---:|---:|
| fib | 3.58 (0.97) | 2.03 (0.60) | 1.76 (0.47) | 2.0 |
| loop | 5.01 (2.52) | 2.43 (0.83) | 1.29 (0.47) | 3.9 |
| sieve | 14.54 (4.96) | 7.61 (2.52) | 5.43 (1.88) | 2.7 |
| nbody | 12.97 (4.90) | 6.99 (2.07) | 4.10 (1.28) | 3.2 |
| trees | 22.50 (6.16) | 16.58 (4.31) | 14.51 (3.75) | 1.6 |
| words | 6.36 (1.88) | 5.14 (1.33) | 4.88 (1.18) | 1.3 |
| json | 14.46 (3.21) | 14.40 (3.15) | 14.41 (3.25) | 1.0 |
| sort | 4.37 (2.25) | 3.52 (1.73) | 3.36 (1.68) | 1.3 |
| closures | 13.36 (3.40) | 10.47 (2.18) | 0.50 (0.18) | 26.7 |
| iface | 8.91 (3.09) | 5.65 (1.62) | 4.61 (1.23) | 1.9 |
| strings | 14.75 (3.13) | 13.42 (2.65) | 13.22 (2.68) | 1.1 |

× is before's instructions over now's. `words`, `json`, `sort` and
`strings` are mostly the libraries' (maps, the JSON codec, sorting,
strings, the allocator): the VM's part of them is small.

## Compiled to C

| benchmark | before | not optimized | optimized | × |
|---|---:|---:|---:|---:|
| fib | 1.28 (0.24) | 1.27 (0.24) | 0.93 (0.18) | 1.4 |
| loop | 0.26 (0.23) | 0.26 (0.23) | 0.34 (0.23) | 0.8 |
| sieve | 5.08 (1.50) | 3.47 (0.99) | 3.26 (1.04) | 1.6 |
| nbody | 8.08 (1.57) | 5.85 (1.36) | 3.56 (0.92) | 2.3 |
| trees | 9.69 (1.84) | 9.69 (1.96) | 7.99 (1.56) | 1.2 |
| words | 4.34 (0.91) | 4.30 (0.96) | 4.30 (0.93) | 1.0 |
| json | 14.31 (3.13) | 14.35 (3.18) | 14.33 (3.18) | 1.0 |
| sort | 2.92 (1.51) | 2.95 (1.50) | 2.92 (1.50) | 1.0 |
| closures | 6.93 (1.14) | 6.40 (1.13) | 0.15 (0.11) | 45.2 |
| iface | 2.36 (0.40) | 2.27 (0.36) | 2.21 (0.37) | 1.1 |
| strings | 12.40 (2.16) | 12.46 (2.37) | 12.40 (2.29) | 1.0 |

`loop` in C retires more instructions but takes the same cycles: it's
bound by its modulo's latency either way.

## Against other interpreters (vm/ref)

| benchmark | Asuka's VM | CPython 3.14.8 | LuaJIT 2.1's interpreter (`-joff`) | Bun 1.4.4 (a JIT) |
|---|---:|---:|---:|---:|
| fib | 1.76 (0.47) | 3.42 (0.80) | 0.88 (0.23) | 0.32 (0.07) |
| loop | 1.29 (0.47) | 19.15 (3.65) | 2.45 (0.81) | 0.46 (0.31) |
| sieve | 5.43 (1.88) | 27.68 (7.27) | 7.93 (3.54) | 0.99 (0.57) |
| nbody | 4.10 (1.28) | 18.41 (4.37) | 4.76 (1.22) | 0.61 (0.17) |
| trees | 14.51 (3.75) | 19.46 (5.11) | 12.18 (3.65) | 2.61 (0.67) |
| closures | 0.50 (0.18) | 20.35 (4.19) | 7.82 (1.92) | 0.65 (0.25) |

