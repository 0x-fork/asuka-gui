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

## With the compiled-C work (merged)

The VM's and the optimizer's work merged with release builds' C
(registers as C locals, direct calls, places; small objects from a
thread's cache), measured again in one session (2026-11-19, load 3 to
45): instructions in billions, cycles in parentheses, the median of 5
(-c: of 9 for closures and sieve). "VM alone" and "C alone" are each
branch by itself.

| benchmark | VM alone | merged | -c: C alone | -c: VM alone | -c: merged |
|---|---:|---:|---:|---:|---:|
| fib | 1.76 (0.34) | 1.71 (0.32) | 0.20 (0.05) | 0.92 (0.15) | 0.19 (0.04) |
| loop | 1.28 (0.41) | 1.28 (0.38) | 0.22 (0.23) | 0.34 (0.23) | 0.22 (0.23) |
| sieve | 5.35 (1.25) | 3.85 (1.00) | 1.17 (0.55) | 3.21 (0.79) | 1.33 (0.54) |
| nbody | 4.07 (0.86) | 4.12 (0.86) | 0.29 (0.07) | 3.56 (0.86) | 0.29 (0.07) |
| trees | 14.44 (2.65) | 12.59 (2.41) | 3.63 (0.71) | 7.97 (1.50) | 3.40 (0.67) |
| words | 4.82 (0.86) | 4.84 (0.87) | 3.87 (0.78) | 4.26 (0.84) | 3.87 (0.75) |
| json | 14.33 (2.58) | 12.36 (2.14) | 11.95 (2.51) | 14.26 (2.88) | 11.95 (2.54) |
| sort | 3.29 (1.41) | 3.31 (1.43) | 2.74 (1.41) | 2.87 (1.39) | 2.74 (1.43) |
| closures | 0.49 (0.12) | 0.50 (0.12) | 3.08 (0.43) | 0.15 (0.11) | 0.15 (0.12) |
| iface | 4.58 (0.69) | 4.50 (0.71) | 1.59 (0.24) | 2.20 (0.31) | 1.53 (0.22) |
| strings | 13.17 (1.84) | 13.20 (1.83) | 11.36 (2.10) | 12.36 (1.95) | 11.35 (1.99) |

On the VM, the same module retires 0.3 to 1.7% more instructions on the
merged runner for nbody, words, sort, closures and strings, in the same
cycles: with the VM branch's ops.h the runner retires what it did, and
what differs there is code the interpreter doesn't run (the C's
helpers), so it's how the compiler lays out the dispatch function. In
C, sieve's inner loop is three instructions longer (the optimizer's
rotated loop gets a second induction variable from clang) in the same
cycles, and closures' loop is the same instructions as the VM branch's
C, 15% more cycles: where its modulo chain lands.
