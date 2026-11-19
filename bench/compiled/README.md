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

Then calls, loops and closures (2026-11-19, the machine shared, load
50 to 300; each the fewest cycles of five interleaved runs, in millions,
with the median of the instructions retired):

| benchmark | before | now | Go | now / Go (cycles) | instructions: before, now, Go |
|---|---:|---:|---:|---:|---:|
| fib | 547 | 502 | 445 | 1.13× | 3,187 / 2,619 / 1,989 |
| sieve | 1,580 | 1,367 | 645 | 2.12× | 3,523 / 2,938 / 1,510 |
| nbody | 1,143 | 883 | 902 | 0.98× | 4,615 / 3,153 / 3,154 |
| trees | 3,229 | 3,187 | 6,807 | 0.47× | 15,495 / 14,764 / 31,277 |
| words | 815 | 815 | 894 | 0.91× | 3,963 / 3,932 / 3,909 |
| json | 2,691 | 2,644 | 3,593 | 0.74× | 13,200 / 13,178 / 18,258 |
| sort | 187 | 192 | 1,379 | 0.14× | 903 / 897 / 2,081 |

What changed (lang/LOWERING.md, Release builds: C): a compiled call's
check of the stack is one compare of the stack pointer, before the
frame is made, the depth no longer counted and passed (fib: a register
and an argument less a call); facts across the control flow, so a
counted loop over an array's indexes checks none and its array (and an
element it changes) is made unique as it's entered (nbody's inner loop:
no index or array checks, one element check a turn); an element's
pointer read as a pointer, not through the union C compilers alias with
every store. fib's remaining 13%: its frame saves three callee-saved
registers (the stack limit among them, which Go keeps in a register of
its own) and each call tests the error it returns.

"Before" is the C cgen wrote before release builds' C kept registers in
C locals and called compiled functions directly (lang/LOWERING.md,
Release builds: C). On an idle machine, before, in ms: fib 925, sieve
655, nbody 4,955, trees 1,575, words 191, json 1,277, sort 396 (Go: 92,
156, 152, 640, 129, 611, 323).

Arrays then stored their elements by type (a `[bool]` a byte each) and
strings were appended in place and joined at once (2026-11-19, load 90
to 450; cycles, the median of five interleaved runs, in millions; "main"
is 72f20c7b):

| benchmark | main | now | Go | now / Go |
|---|---:|---:|---:|---:|
| sieve | 1,473 | 688 | 624 | 1.10× |
| words | 785 | 585 | 970 | 0.60× |
| json | 2,738 | 2,689 | 3,760 | 0.72× |
| sort | 217 | 200 | 1,427 | 0.14× |

sieve's array is 50 MB now (it was 400 MB, and copied once more by its
first write: 765 MB at its peak; Go's: 52). What's left of it is the
loop: each write checks the array is unique and the index's add for
overflow, which Go's doesn't.

`go run ./bench/compiled/run` prints the wall times; under that load
they said more about the scheduler than the programs (a single-threaded
run descheduled most of the time), so take them again on an idle
machine before comparing.

## The allocator: mimalloc (2026-11-19)

The core's ak_alloc, ak_realloc and ak_free were the C library's
malloc; on Linux arm64 half of json's time was in glibc's (perf:
malloc_consolidate 10.6%, _int_malloc 9.2%, unlink_chunk 3.9%,
_int_free_chunk 3.2%, calloc 2.4%, free 2.2%). They're mimalloc's now
(core/src/base/alloc.c; DESIGN.md says how). Before and after are the
same commit's code but for the allocator.

Linux arm64 (the `asuka-linux-test-clang` image under OrbStack on the
M5, glibc 2.41, clang; Go 1.27.1). Docker's VM has no performance
counters, so CPU time (`perf stat -e task-clock`), the least of five
interleaved runs, in ms, taken while the VM was quiet (load 3); Go's
counts its collector's threads, so its wall time is beside it.

| benchmark | glibc's malloc | mimalloc | Go (wall) | now / Go |
|---|---:|---:|---:|---:|
| fib | 161 | 159 | 143 (140) | 1.11× |
| trees | 712 | 636 | 1,725 (1,190) | 0.37× |
| words | 277 | 197 | 360 (280) | 0.55× |
| json | 1,109 | 708 | 1,048 (1,030) | 0.68× |
| sort | 69 | 69 | 531 (640) | 0.13× |

Taken again later with the machine loaded (VM load 13, every program
slower and noisier: fib's CPU time doubled) the ratios held: json 0.55×
of glibc's time, words 0.53×, trees 0.83×; and statically linked with
musl (`--target linux-arm64`, Zig), json 0.82× of musl's malloc's time,
words 0.58×, trees 0.87×.

Peak RSS (MB), which load doesn't change:

| benchmark | glibc | mimalloc | musl | mimalloc, musl | Go |
|---|---:|---:|---:|---:|---:|
| fib | 2.0 | 2.7 | 1.0 | 1.3 | 2.8 |
| trees | 26.1 | 18.8 | 33.0 | 17.4 | 47.2 |
| words | 164.5 | 166.2 | 285.8 | 164.9 | 137.9 |
| json | 143.8 | 127.5 | 188.0 | 126.1 | 185.4 |
| sort | 78.3 | 98.9 | 77.4 | 97.5 | 152.6 |

sort's peak is higher: its array of 5 million ints grows by doubling,
and mimalloc copies a block that grows (Go's append does too, and
jemalloc), where glibc's and musl's realloc remap the pages (mremap), so
for a moment the old and the new buffer are both there. Handing big
blocks to the C library's realloc instead cost CPU time on Linux (sort
71 → 89 ms) and made macOS's peaks higher (Apple's large realloc
copies too once mimalloc's reservations sit next to the block), so
they're mimalloc's.

macOS (the M5; cycles, the fewest of seven interleaved runs, in
millions, at load 50 to 200):

| benchmark | Apple's malloc | mimalloc | Go | now / Go |
|---|---:|---:|---:|---:|
| fib | 518 | 537 | 415 | 1.29× |
| sieve | 1,376 | 1,399 | 639 | 2.19× |
| nbody | 1,144 | 1,138 | 870 | 1.31× |
| trees | 3,219 | 2,815 | 6,928 | 0.41× |
| words | 767 | 582 | 895 | 0.65× |
| json | 2,620 | 2,374 | 3,409 | 0.70× |
| sort | 164 | 214 | 1,402 | 0.15× |

Peak memory (MB): json 168 → 127, words 179 → 166, trees 17.5 → 18.1,
sort 80 → 98 (as above).

A spike's memory goes back: 3 million blocks of 16 to 216 bytes (390
MB) made and freed, then the thread quiet. glibc's malloc keeps all 399
MB, Apple's 152 MB, Android's scudo 52 MB; mimalloc keeps them until
the thread trims (the main thread two seconds after a turn of work that
grew its heap, an idle spawn worker likewise), then 27 MB on Linux and
macOS, 30 MB on Android.

Allocating and freeing blocks of 16 to 216 bytes (a window of 4,096
live), ns a pair: Linux glibc 40 to 67, mimalloc 21 to 23; macOS 28
to 38, 17 to 19; Android's scudo (the arm64 emulator, Android 17, 16
KB pages: slow and noisy) 332 to 568, mimalloc 100 to 252. On the web
(wasm under Node's WASI) mimalloc took 285 ns to dlmalloc's 377, but
its linear memory grew to 48 MB to dlmalloc's 17, and a module's memory
never shrinks: the web keeps dlmalloc.

What it costs in size, hello world (`asuka new --bin`, `asuka build`),
in bytes:

| target | before | mimalloc | more |
|---|---:|---:|---:|
| macOS arm64 | 703,168 | 803,680 | 100,512 |
| Linux arm64, glibc | 855,080 | 986,344 | 131,264 |
| Linux arm64, musl (static) | 833,904 | 958,288 | 124,384 |
| Linux amd64, musl (static) | 860,096 | 988,512 | 128,416 |
| Windows amd64 | 831,488 | 956,928 | 125,440 |
| Windows arm64 | 715,776 | 835,072 | 119,296 |

(88 KB of it is code on macOS arm64; its 59 KB of process state is zero
fill.)

Windows wasn't measured: no Windows machine was reachable; the arm64
build ran bench/compiled correctly under Wine.

## Packed arrays and the pass on calls, loops and closures, merged (2026-11-19)

The two branches measured alone and merged (the integration branch:
main, mimalloc, packed arrays; then calls, loops and closures merged
into it), the machine shared (load 50 to 300): cycles, the fewest of
five interleaved runs, and instructions retired, the median, the whole
process, in millions. "packed arrays" has no fused loops; "calls and
loops" no packed arrays and the C library's malloc.

| benchmark | packed arrays | calls and loops | merged | Go | merged / Go | instructions: packed, calls, merged, Go |
|---|---:|---:|---:|---:|---:|---:|
| fib | 458 | 434 | 443 | 386 | 1.15× | 3,181 / 2,611 / 2,612 / 1,982 |
| sieve | 636 | 1,517 | 632 | 616 | 1.03× | 2,178 / 2,904 / 1,691 / 1,488 |
| nbody | 971 | 740 | 685 | 749 | 0.91× | 4,602 / 3,141 / 3,143 / 3,134 |
| trees | 2,577 | 2,961 | 2,688 | 6,909 | 0.39× | 14,147 / 14,670 / 13,365 / 30,446 |
| words | 479 | 728 | 484 | 847 | 0.57× | 2,401 / 3,930 / 2,372 / 3,680 |
| json | 2,300 | 2,386 | 2,336 | 3,118 | 0.75× | 11,660 / 13,137 / 11,679 / 18,044 |
| sort | 142 | 138 | 145 | 1,352 | 0.11× | 847 / 878 / 853 / 2,098 |

sieve keeps both: a [bool] a byte each, its loop's index and array
checks gone (1.69 G instructions to Go's 1.49 G). nbody keeps its
element's pointer (AK_REFS: read as a pointer, not through the union)
and runs as Go's. What's still behind Go: fib's frame and the error
each call returns (2.6 G instructions to 2.0 G), sieve's checked adds
(3% in cycles).

Merging needed two things beyond the conflicts. An array's push read
its len and cap as one 8-byte word right after the last push stored
len's 4 bytes, which a load can't be forwarded from: each push waited
for the one before (a fused map over 100,000 ints: 284 M cycles, 96 M
with cap read on its own; the calls branch's 144 M). And an int read
from an array asked the array how it stores its elements (an [u8]'s
are packed): a function with no sized integers in it doesn't any more
(bytecode.FSized), so a reduce over [int] is the calls branch's loop
again (35 M cycles to 42 M).

## Calls, unchecked bounded ints, the stack limit in x28 (2026-11-19)

The machine shared (load 90 to 280): cycles, the fewest of five
interleaved runs, and instructions retired, the median, the whole
process, in millions. "Before" is c43394e1 (the merge above).

| benchmark | cycles before | now | Go | now / Go | instructions before | now | Go |
|---|---:|---:|---:|---:|---:|---:|---:|
| fib | 487 | 422 | 446 | 0.95× | 2,616 | 1,982 | 1,985 |
| sieve | 628 | 604 | 614 | 0.98× | 1,705 | 1,366 | 1,505 |
| nbody | 807 | 856 | 817 | 1.05× | 3,145 | 3,145 | 3,140 |
| trees | 2,701 | 2,548 | 6,605 | 0.39× | 13,384 | 12,959 | 30,792 |
| words | 491 | 475 | 856 | 0.55× | 2,385 | 2,402 | 3,826 |
| json | 2,273 | 2,236 | 3,494 | 0.64× | 11,137 | 11,151 | 18,329 |
| sort | 206 | 172 | 1,389 | 0.12× | 879 | 880 | 2,144 |

nbody's instructions are as they were (and as Go's): its cycles differ
with the load. What changed (lang/LOWERING.md, Release builds: C):

- fib. An error leaves a compiled function through a cold call
  (ak_cg_unwind), so clang lays the error's paths out of the way: each
  call's test of the error it returned fell through to the error's block
  and jumped back (422 M cycles from 487 M with the same instructions).
  On arm64 the thread's stack limit is in x28 (the app's C compiled with
  -ffixed-x28, as Go keeps its g in R28; each way in from the core sets
  and restores it), so the check is `cmp sp, x28; b.lo` and no register
  of a recursing function holds the limit; a direct body's arguments
  come before its closure, so its first is in x0, where its result goes:
  11 instructions a base case and 20 a recursive call, as Go's.
- sieve. The facts across the control flow know an int's magnitude: an
  index or a length is below 2^32, so `j += i` of two indexes can't
  overflow and isn't checked (it's opaque to clang's loop analysis, as
  the checked add was: as a plain add, clang worked out the inner loop's
  trip count with a 128-bit division on every turn of the outer one).
  1,366 M instructions to Go's 1,505 M.
