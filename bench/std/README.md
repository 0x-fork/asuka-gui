# The standard library's benchmarks

The same work in Asuka (`asuka/main.asuka`), Go (`go/main.go`) and Bun
(`bun/main.js`), on the same inputs: strings, maps and sets, sorting,
regex, files and stdout, CSV and TOML, crypto and compression, bigint,
time, template, semver, collections, Seq pipelines. Each program sets up
its inputs, then times the work and prints its result and the
nanoseconds it took on stderr; the runner checks that every language's
result is the same, so each did the same work (gzip's compressed size
aside: every deflate makes its own).

```sh
go run ./bench/std/run                           # every benchmark, 5 runs each
go run ./bench/std/run -only sha256,gzip -runs 9
go run ./bench/std/run -asuka before=/path/to/an/older/stdbench   # another build beside
```

It builds `asuka/` (`asuka build`, compiled to C) and `go/` (its own
module: `golang.org/x/crypto` for argon2, bcrypt and ChaCha20-Poly1305,
BurntSushi's TOML, Masterminds' semver and Google's B-tree, what Go has
no standard package for), runs each benchmark once in each language to
warm up, then `-runs` times interleaved, and prints the median and the
median absolute deviation, in milliseconds, and Asuka's median over Go's.
What Bun has no equivalent of is n/a.

The HTTP servers are timed apart: `asuka-http/` (its own program, so
the benchmarks above don't link std/http), and Go's and Bun's `serve
<port>`, answer `GET /hello` (text) and `GET /json`; `go/load` drives
one with keep-alive connections:

```sh
go run ./cmd/asuka build bench/std/asuka-http && bench/std/asuka-http/dist/stdbench-http 8080 &
# or: (cd bench/std/go && go run . serve 8080 &), bun bench/std/bun/main.js serve 8080 &
(cd bench/std/go && go run ./load -url http://127.0.0.1:8080/hello -c 50 -d 10s)
```

## Results

An Apple M5 (10 cores, 32 GB), macOS 27, idle, on 2026-11-19: Go 1.27.3,
Bun 1.4.4, Asuka compiled to C (`asuka build`). "asuka before" is the
same program built from the commit before this pass (f9bf57d2). files
runs after the first run cached the file, so it's memory's speed; files,
lines and print write to a temporary directory and /dev/null.

Apple M5, darwin/arm64, 5 runs (median ± median absolute deviation, ms)

| benchmark | asuka | go | bun | asuka before | asuka/go |
|---|---:|---:|---:|---:|---:|
| interp | 116 ± 1.31 | 166 ± 1.61 | 115 ± 0.84 | 253 ± 3.62 | 0.70× |
| concat | 148 ± 2.84 | 29.6 ± 0.56 | 13.2 ± 0.22 | 149 ± 2.99 | 4.98× |
| split | 287 ± 6.13 | 95.6 ± 0.60 | 276 ± 10.1 | 340 ± 3.36 | 3.00× |
| find | 103 ± 0.07 | 139 ± 0.34 | 57.8 ± 0.76 | 343 ± 4.99 | 0.74× |
| case | 312 ± 2.09 | 310 ± 1.82 | 93.9 ± 0.65 | 339 ± 1.24 | 1.00× |
| count | 0.69 ± 0.06 | 59.9 ± 0.28 | 165 ± 1.83 | 6709 ± 8.88 | 0.01× |
| num | 159 ± 3.78 | 125 ± 0.42 | 94.1 ± 0.31 | 1761 ± 9.28 | 1.27× |
| fmt | 451 ± 6.65 | 167 ± 0.97 | 465 ± 2.49 | 563 ± 1.93 | 2.69× |
| mapint | 81.1 ± 0.27 | 90.6 ± 1.64 | 102 ± 2.48 | 79.8 ± 1.91 | 0.89× |
| mapstr | 76.1 ± 0.63 | 93.4 ± 1.98 | 189 ± 2.43 | 134 ± 2.12 | 0.82× |
| set | 95.5 ± 7.06 | 74.9 ± 0.60 | 84.5 ± 4.34 | 93.1 ± 0.69 | 1.27× |
| sortint | 21.5 ± 0.36 | 285 ± 0.87 | 40.9 ± 0.11 | 358 ± 0.74 | 0.08× |
| sortstr | 136 ± 0.74 | 118 ± 4.47 | 66.6 ± 4.71 | 134 ± 1.47 | 1.15× |
| sortby | 246 ± 1.00 | 1421 ± 21.6 | 293 ± 2.82 | 281 ± 1.69 | 0.17× |
| regex | 787 ± 7.19 | 1260 ± 141 | 103 ± 2.09 | 1416 ± 49.6 | 0.62× |
| files | 5.47 ± 0.29¹ | 3.59 ± 0.02 | 6.68 ± 0.22 | 32.6 ± 0.27 | 1.52× |
| lines | 22.1 ± 0.69 | 6.93 ± 0.55 | 13.7 ± 1.44 | 23.6 ± 0.72 | 3.19× |
| print | 431 ± 2.40 | 207 ± 1.02 | 200 ± 1.93 | 477 ± 2.72 | 2.08× |
| csv | 113 ± 1.58 | 47.2 ± 0.86 | n/a | 159 ± 2.11 | 2.38× |
| toml | 58.3 ± 0.24 | 200 ± 2.28 | 5861 ± 71.1 | 77.3 ± 0.51 | 0.29× |
| sha256 | 66.1 ± 0.54 | 66.2 ± 0.06 | 69.9 ± 0.59 | 544 ± 13.0 | 1.00× |
| sha512 | 115 ± 0.54 | 117 ± 2.62 | 124 ± 0.50 | 334 ± 2.43 | 0.99× |
| hmac | 57.5 ± 0.07 | 98.3 ± 0.24 | 113 ± 0.42 | 451 ± 0.72 | 0.59× |
| aesgcm | 30.4 ± 0.77 | 31.6 ± 1.10 | 57.2 ± 8.58 | 1281 ± 4.99 | 0.96× |
| chacha | ≈108² | 164 ± 1.52 | n/a | 293 ± 3.38 | 0.66×² |
| argon2 | ≈29² | 53.0 ± 0.95 | n/a | 125 ± 0.81 | 0.55×² |
| bcrypt | 176 ± 0.48 | 183 ± 0.37 | 171 ± 0.62 | 176 ± 0.25 | 0.96× |
| gzip | 3460 ± 4.95 | 673 ± 1.68 | 863 ± 5.68 | 3781 ± 7.39 | 5.15× |
| bigint | 20.5 ± 0.09 | 7.40 ± 0.02 | 14.7 ± 0.06 | 20.5 ± 0.05 | 2.77× |
| modpow | 46.4 ± 0.09 | 35.7 ± 0.05 | 33.3 ± 0.23 | 46.2 ± 0.15 | 1.30× |
| time | 138 ± 1.73 | 11.3 ± 0.10 | 66.9 ± 1.33 | 225 ± 1.75 | 12.22× |
| template | 188 ± 1.99 | 32.9 ± 0.72 | n/a | 198 ± 0.50 | 5.71× |
| semver | 269 ± 2.34 | 160 ± 3.82 | 241 ± 1.99 | 291 ± 3.00 | 1.68× |
| heap | 371 ± 2.01 | 151 ± 0.62 | n/a | 373 ± 3.57 | 2.45× |
| sortedmap | 1221 ± 20.7 | 301 ± 2.70 | n/a | 1219 ± 17.0 | 4.06× |
| seq² | 130 ± 1.85 | 2.37 ± 0.00 | 4.16 ± 0.18 | 130 ± 4.46 | 54.85× |

¹ After the table's run: fs.read reads at the file's size now (aaae01aa);
three runs.

² After the table's run: ChaCha20-Poly1305 and Argon2 on the vector unit,
Argon2's lanes on threads (core/src/std/chacha.c, argon2.c). The machine
was loaded then (load average 60 to 210), so these are counted rather
than timed: CPU cycles for the same work, five interleaved runs each,
Asuka's over Go's (chacha 0.58 G cycles to Go's 0.88 G, 1.9 G
instructions to 2.2 G; argon2 0.69 G cycles across its four threads to
Go's 1.25 G, 1.34 G instructions to 3.1 G), times Go's idle median.
Before, chacha took 1.53 G cycles and argon2 0.74 G on one thread.

Strings, arrays and number formatting were then taken further (packed
arrays, strings appended in place, joined at once and cut into slabs,
formats written in place, floats printed and read without libc, string
sorts by prefix keys; 2026-11-19). The machine was shared with other
builds (load 90 to 450), so wall time said more about the scheduler
than the programs; this table is each program's CPU cycles (`time
-l`'s, Apple's counters; the median of five interleaved runs, in
millions, the setup included). Go's cycles count its collector's
threads too, so on what allocates a lot they flatter Asuka a little.
"main" is the commit before (72f20c7b).

| benchmark | main | now | go | now/go |
|---|---:|---:|---:|---:|
| concat | 784 | 209 | 184 | 1.14× |
| split | 2,061 | 987 | 723 | 1.37× |
| fmt | 2,322 | 663 | 1,057 | 0.63× |
| set | 648 | 561 | 576 | 0.97× |
| sortstr | 1,091 | 548 | 1,147 | 0.48× |
| num | 704 | 684 | 888 | 0.77× |
| interp | 569 | 616 | 1,218 | 0.51× |
| find | 931 | 885 | 810 | 1.09× |
| mapint | 667 | 566 | 585 | 0.97× |
| mapstr | 1,020 | 1,071 | 1,469 | 0.73× |
| semver | 1,010 | 915 | 1,078 | 0.85× |
| time | 315 | 262 | 96 | 2.73× |

What's still slow is mostly what the compiled code does around the
library, not the library: split makes a string object a piece (in
slabs now, a bump and a count each, where Go's substrings are two words
in the array), and its arrays live to the end of the statement's
registers' next use; seq, heap, sortedmap, template, time and semver
are Asuka code (closure calls a step, structs copied); gzip at level 6
is miniz's matcher, as slow as zlib's on this repetitive text (zlib
took 4.5 s), where Go's compressor is far faster; regex patterns that
start with `\w` still run the Pike VM a character at a time (Go's
regexp does too; Rust's regex crate has a lazy DFA).

The core's allocator is mimalloc now (bench/compiled/README.md: why,
and what it costs). The benchmarks that allocate most, Linux arm64
(OrbStack on the M5, glibc 2.41, Go 1.27.3), the fewest ms of seven
interleaved runs (the VM loaded: they're slower than on the Mac, and
noisy; concat, mapstr and csv again from five runs at load 3 in
brackets), and peak RSS in MB:

| benchmark | glibc's malloc | mimalloc | musl's | mimalloc, musl | Go | RSS glibc → mimalloc |
|---|---:|---:|---:|---:|---:|---:|
| split | 1,098 | 350 | 654 | 379 | 211 | 183 → 225 |
| concat | 219 (134) | 166 (116) | 304 | 120 | 65 (45) | 3.9 → 4.7 |
| mapstr | 296 (135) | 299 (135) | 436 | 417 | 493 (206) | 134 → 111 |
| csv | 302 (190) | 173 (127) | 317 | 173 | 116 (81) | 180 → 158 |
| template | 243 | 198 | 263 | 195 | 79 | 4.8 → 5.6 |
| fmt | 1,212 | 1,176 | 1,237 | 1,027 | 442 | 3.9 → 4.7 |

On the Mac (the fewest ms of seven, at load 50 to 200) concat went from
1,068 to 368, csv 885 to 569, fmt 3,279 to 2,728, template 269 to 217;
split's peak memory went from 161 to 223 MB (its arrays of millions of
strings copied as they double: bench/compiled/README.md, sort). What's
behind Go there is still allocating a string a piece.

² Go's and Bun's seq computed the lazy pipeline once and printed it
twice; they now do the eager pipeline's work too (an array of the
numbers, then map, filter, map and reduce, a step at a time), as
Asuka's does. The numbers below are of that.

Then the compiler's pass on calls, loops and closures (lang/LOWERING.md:
Seq pipelines and arrays' closure methods lowered as loops, closures
inlined there, typed closure calls, facts across loops, scalar
replacement, a return releasing only what it holds), with std/template
and std/collections' own changes, measured with the machine shared (load
50 to 300, so in cycles, the fewest of five interleaved runs, and
instructions retired, the median; the whole process, setup included,
in millions):

| benchmark | cycles before | now | Go | now / Go | instructions before | now | Go |
|---|---:|---:|---:|---:|---:|---:|---:|
| heap | 1,495 | 1,046 | 1,139 | 0.92× | 4,101 | 2,140 | 2,401 |
| sortedmap | 5,428 | 3,790 | 2,344 | 1.62× | 11,106 | 5,309 | 2,467 |
| seq | 472 | 117 | 121 | 0.97× | 3,125 | 583 | 335 |
| semver | 1,010 | 872 | 1,033 | 0.84× | 4,258 | 3,412 | 3,891 |
| template | 521 | 370 | 191 | 1.94× | 2,209 | 1,757 | 800 |

The other benchmarks' instructions are as they were (within 1%).
What's still slower than Go: sortedmap, whose nodes' arrays are a
header and a buffer apart (one more dependent load a level than Go's
slices), whose binary search branches where Go's selects, and whose
order is a closure called a comparison; template, a tree-walking
interpreter over json.Value data that renders its data through
json.encode and json.parse (a fifth of its time), where Go's
text/template reads the struct by reflection.

HTTP, 50 keep-alive connections, 5 s after 1 s of warm-up, the load
generator on the same machine:

| server | /hello req/s | p50 | p99 | /json req/s | p50 | p99 |
|---|---:|---:|---:|---:|---:|---:|
| Asuka (std/http) | 105,291 | 456 µs | 937 µs | 107,126 | 460 µs | 750 µs |
| Go (net/http) | 93,730 | 526 µs | 941 µs | 89,415 | 542 µs | 1.12 ms |
| Bun (Bun.serve) | 110,819 | 396 µs | 877 µs | 114,095 | 383 µs | 847 µs |

## Packed arrays and the pass on calls, loops and closures, merged (2026-11-19)

The two branches measured alone and merged (the integration branch:
main, mimalloc, packed arrays; then calls, loops and closures merged
into it), the machine shared (load 50 to 300): cycles, the fewest of
five interleaved runs, and instructions retired, the median, the whole
process, in millions. "packed arrays" has no fused loops; "calls and
loops" no packed arrays and the C library's malloc.

| benchmark | packed arrays | calls and loops | merged | Go | merged / Go | instructions: packed, calls, merged, Go |
|---|---:|---:|---:|---:|---:|---:|
| interp | 416 | 443 | 418 | 899 | 0.46× | 2,406 / 2,811 / 2,405 / 5,381 |
| concat | 154 | 569 | 156 | 140 | 1.11× | 1,103 / 4,182 / 1,107 / 934 |
| split | 695 | 1,523 | 706 | 574 | 1.23× | 3,629 / 9,169 / 3,651 / 2,483 |
| find | 730 | 864 | 732 | 763 | 0.96× | 2,285 / 3,015 / 2,281 / 2,121 |
| case | 1,653 | 1,784 | 1,669 | 1,464 | 1.14× | 8,292 / 8,946 / 8,291 / 7,820 |
| count | 298 | 378 | 294 | 437 | 0.67× | 1,578 / 2,260 / 1,575 / 2,699 |
| num | 546 | 651 | 506 | 823 | 0.61× | 2,610 / 3,458 / 2,586 / 3,932 |
| fmt | 610 | 2,140 | 624 | 970 | 0.64× | 3,226 / 10,272 / 3,228 / 4,579 |
| mapint | 444 | 451 | 427 | 482 | 0.89× | 821 / 898 / 818 / 1,044 |
| mapstr | 636 | 707 | 611 | 1,331 | 0.46× | 2,656 / 3,165 / 2,640 / 4,134 |
| set | 495 | 546 | 486 | 525 | 0.93× | 840 / 914 / 835 / 995 |
| sortint | 157 | 151 | 168 | 1,399 | 0.12× | 882 / 915 / 892 / 2,168 |
| sortstr | 455 | 768 | 453 | 821 | 0.55× | 1,150 / 2,355 / 1,182 / 2,894 |
| sortby | 1,225 | 1,174 | 1,062 | 6,897 | 0.15× | 3,708 / 3,435 / 3,225 / 32,685 |
| regex | 4,455 | 4,444 | 4,440 | 5,834 | 0.76× | 22,672 / 23,838 / 22,625 / 35,770 |
| files | 312 | 427 | 322 | 153 | 2.10× | 1,608 / 2,296 / 1,605 / 510 |
| lines | 396 | 506 | 378 | 189 | 2.00× | 1,936 / 2,708 / 1,920 / 591 |
| print | 2,947 | 2,935 | 2,960 | 3,033 | 0.98× | 12,845 / 13,068 / 12,847 / 13,187 |
| csv | 544 | 729 | 598 | 817 | 0.73× | 2,777 / 3,899 / 2,788 / 3,685 |
| toml | 247 | 273 | 257 | 1,396 | 0.18× | 1,146 / 1,417 / 1,141 / 5,088 |
| sha256 | 1,901 | 2,514 | 1,946 | 881 | 2.21× | 8,203 / 11,579 / 8,228 / 2,557 |
| sha512 | 2,154 | 2,866 | 2,257 | 1,131 | 2.00× | 8,660 / 12,030 / 8,680 / 3,049 |
| hmac | 271 | 272 | 268 | 580 | 0.46× | 896 / 1,031 / 897 / 2,454 |
| aesgcm | 1,700 | 2,338 | 1,824 | 804 | 2.27× | 8,714 / 12,088 / 8,730 / 2,933 |
| chacha | 1,844 | 3,659 | 2,096 | 1,407 | 1.49× | 9,752 / 15,635 / 9,777 / 4,276 |
| argon2 | 665 | 650 | 683 | 1,231 | 0.55× | 1,333 / 1,891 / 1,331 / 3,119 |
| bcrypt | 763 | 760 | 760 | 778 | 0.98× | 1,236 / 1,233 / 1,235 / 1,216 |
| gzip | 16,106 | 16,538 | 16,193 | 3,366 | 4.81× | 32,842 / 36,166 / 32,915 / 14,566 |
| bigint | 93 | 95 | 93 | 46 | 2.02× | 401 / 418 / 398 / 186 |
| modpow | 208 | 207 | 208 | 167 | 1.25× | 1,099 / 1,099 / 1,099 / 847 |
| time | 179 | 219 | 180 | 64 | 2.81× | 1,280 / 1,581 / 1,255 / 434 |
| template | 348 | 303 | 264 | 157 | 1.68× | 1,820 / 1,751 / 1,439 / 777 |
| semver | 565 | 627 | 449 | 777 | 0.58× | 2,937 / 3,396 / 2,129 / 3,855 |
| heap | 678 | 465 | 462 | 708 | 0.65× | 4,329 / 2,112 / 2,075 / 2,371 |
| sortedmap | 2,808 | 1,927 | 1,668 | 1,333 | 1.25× | 10,258 / 5,204 / 3,995 / 2,343 |
| seq | 409 | 109 | 82 | 91 | 0.90× | 3,171 / 568 / 440 / 316 |

heap, sortedmap, seq, semver and template keep the calls branch's gains
(B-tree degree 63, fused pipelines, std/template's loops), the string
benchmarks the packed branch's (concat, split, fmt). seq is now faster
than Go's (82 M cycles to 91 M): its fused map and filter append with
cap read on its own (bench/compiled's README). The cycles that differ
with the instructions the same (chacha, aesgcm, csv) are the load's.
