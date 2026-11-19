# Benchmarks: plain computation, compiled

Asuka programs compiled to C (`asuka build`, what an app and a program
ship) against the same programs in Go, Zig and Bun: the same
algorithms, the same inputs, the same output.

| benchmark | what it does | what it stresses |
|---|---|---|
| fib | `fib(38)`, recursively | calls, checked int arithmetic |
| sieve | the primes below 50,000,000, a `[bool]` sieve | array writes, memory |
| nbody | 5,000,000 steps of five bodies (`[Body]`, a struct of floats) | an array of structs' fields read and changed in place, float math |
| trees | binary trees of depth 18 (`Tree?` children), made and walked | allocation, reference counting |
| words | 2,000,000 words joined, split and counted in a map | strings, maps, sorting with a closure |
| json | 200,000 structs to JSON and back, five times | `std/json`'s encoder and typed decoder |
| sort | 5,000,000 ints sorted | the array's sort |

```sh
go run ./bench/compiled/run                       # every benchmark, in each language installed
go run ./bench/compiled/run -only fib,nbody -runs 10
go run ./bench/compiled/run -langs asuka,go
go run ./bench/compiled/run -asuka old=/path/to/langbench   # another Asuka build beside it
```

The runner builds each program (`asuka build`, `go build`, `zig
build-exe -O ReleaseFast`; Bun runs the script), checks that each
prints the same, and times them with
[hyperfine](https://github.com/sharkdp/hyperfine): a warm-up run, then
`-runs` runs, their mean and standard deviation. The Asuka programs are
also `bench` blocks (`asuka/bench.asuka`, smaller), so

```sh
cd bench/compiled/asuka && asuka test --bench     # compiled to C; --vm: on the VM
```

times them as any package's benches, and `--compare` shows what a change
did.

## Results

Apple M5 (macOS 27), Go 1.27.3, Zig 0.17.0, Bun 1.4.4; 2026-11-19.

The machine was shared with heavy parallel builds while these were
taken (load averages of 100 to 450), which slows wall time by up to 3×
and unevenly, so the table below is each program's CPU cycles (`time
-l`'s "cycles elapsed", Apple's performance counters), the fewest of
five runs, in millions: what load changes least. Go's trees and json
count its collector's threads too (on an idle machine their wall time
is 640 and 611 ms).

| benchmark | Asuka before | Asuka now | Go | now / Go |
|---|---:|---:|---:|---:|
| fib | 4,167 | 604 | 408 | 1.48× |
| sieve | 3,594 | 1,426 | 615 | 2.32× |
| nbody | 26,274 | 1,037 | 805 | 1.29× |
| trees | 8,473 | 3,393 | 6,888 | 0.49× |
| words | 916 | 789 | 887 | 0.89× |
| json | 6,440 | 5,232 | 3,411 | 1.53× |
| sort | 1,802 | 1,734 | 1,369 | 1.27× |

With lang/ir's optimizer too (the VM's branch merged: its constants,
fused comparisons and branches, IDXF and SETIF, inlining), measured
again in one session (2026-11-19, load 3 to 45; cycles, the fewest of
five, in millions):

| benchmark | C alone | with the optimizer | Go | now / Go |
|---|---:|---:|---:|---:|
| fib | 478 | 413 | 371 | 1.11× |
| sieve | 1,495 | 1,499 | 637 | 2.35× |
| nbody | 768 | 780 | 648 | 1.20× |
| trees | 2,582 | 2,376 | 5,637 | 0.42× |
| words | 629 | 625 | 718 | 0.87× |
| json | 4,224 | 4,198 | 2,758 | 1.52× |
| sort | 1,594 | 1,607 | 1,383 | 1.16× |

"Before" is the C cgen wrote before release builds' C kept registers in
C locals and called compiled functions directly (lang/LOWERING.md,
Release builds: C). On an idle machine, before, in ms: fib 925, sieve
655, nbody 4,955, trees 1,575, words 191, json 1,277, sort 396 (Go: 92,
156, 152, 640, 129, 611, 323).

What's left: sieve's `[bool]` elements are 8 bytes each (400 MB for
50M, Go's 50 MB): the same loop in C with 8-byte elements takes as long
as Asuka's, with bytes as long as Go's. json's time is mostly formatting
floats with the C library's `snprintf`; with the shortest-form
formatter of the standard library's performance work it's 2,606 (0.76×
Go).

`go run ./bench/compiled/run` prints the wall times; under that load
they said more about the scheduler than the programs (a single-threaded
run descheduled most of the time), so take them again on an idle
machine before comparing.
