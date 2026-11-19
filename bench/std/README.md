# The standard library's benchmarks

The same work in Asuka (`asuka/main.asuka`), Go (`go/main.go`) and Bun
(`bun/main.js`), on the same inputs: strings, maps and sets, sorting,
regex, files and stdout, CSV and TOML, URLs, Base64 and hex, HTML
escaping, zip and tar archives, hashes and checksums, crypto and
compression, bigint, time, template, semver, collections (a heap, a
sorted map's sets and gets, and its nearest keys, walks, ranges and
removals: sortedops), Seq pipelines. Each program sets up
its inputs, then times the work and prints its result and the
nanoseconds it took on stderr; the runner checks that every language's
result is the same, so each did the same work (gzip's compressed size
aside: every deflate makes its own).

```sh
go run ./bench/std/run                           # every benchmark, 5 runs each
go run ./bench/std/run -only sha256,gzip -runs 9
go run ./bench/std/run -asuka before=/path/to/an/older/stdbench   # another build beside
go run ./bench/std/run -instr -runs 3            # cycles and instructions a run, not time
```

It builds `asuka/` (`asuka build`, compiled to C) and `go/` (its own
module: `golang.org/x/crypto` for argon2, bcrypt and ChaCha20-Poly1305,
BurntSushi's TOML, Masterminds' semver and Google's B-tree, what Go has
no standard package for), runs each benchmark once in each language to
warm up, then `-runs` times interleaved, and prints the median and the
median absolute deviation, in milliseconds, and Asuka's median over Go's.
What Bun has no equivalent of is n/a.

A busy machine moves wall time a lot (the more a benchmark allocates,
the more), so `-instr` counts cycles and instructions retired instead
(macOS's `/usr/bin/time -l`, Linux's `perf stat`): each program does its
work `STDBENCH_REPS` times, and a benchmark's count is a process doing it
k times less one doing it once, over k − 1, its setup cancelled out (k
is `-reps`, more where the setup is long beside the work).

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

An Apple M5 (10 cores, 32 GB), macOS 27, on 2026-11-19: Go 1.27.3,
Asuka compiled to C (`asuka build`), the core's allocator mimalloc. The machine was shared and loaded
(load average 50 to 400), which moves wall time by tens of percent, so
these are counted: `go run ./bench/std/run -instr -runs 3 -reps 4 -langs
asuka,go`, millions of CPU cycles a run (the median of three
interleaved runs; summed over threads for argon2) and the ratio of
instructions retired, which load hardly moves. Cycles still vary by
5–15% between passes for the benchmarks that allocate most (time,
heap, csv); instructions by under 2%. "asuka before" is the same work
built from cd564cc2, before this pass, counted the same way; – is a
benchmark that's new since, or (print) one whose earlier count wrote
somewhere else. files runs after the first run cached the file, so
it's memory's speed; files, lines and print write to a temporary
directory and /dev/null.

| benchmark | asuka | go | asuka before | asuka/go | instructions asuka/go |
|---|---:|---:|---:|---:|---:|
| interp | 454 | 983 | 514 | 0.46× | 0.44× |
| concat | 373 | 148 | 805 | 2.52× | 2.11× |
| split | 853 | 480 | 1454 | 1.78× | 1.91× |
| find | 452 | 626 | 467 | 0.72× | 0.45× |
| case | 1902 | 1447 | 1910 | 1.31× | 0.92× |
| count | ≈0 | 324 | ≈0 | ≈0 | ≈0 |
| num | 597 | 782 | 775 | 0.76× | 0.76× |
| fmt | 1959 | 934 | 2315 | 2.10× | 1.84× |
| mapint | 409 | 448 | 658 | 0.91× | 0.86× |
| mapstr | 350 | 621 | 564 | 0.56× | 0.94× |
| set | 460 | 425 | 600 | 1.08× | 0.92× |
| sortint | 105 | 1214 | 94.2 | 0.09× | 0.28× |
| sortstr | 722 | 663 | 764 | 1.09× | 1.09× |
| sortby | 1198 | 6807 | 1254 | 0.18× | 0.10× |
| regex | 572 | 6368 | 4397 | 0.09× | 0.06× |
| files | 8.94 | 13.3 | 9.50 | 0.67× | 1.08× |
| lines | 42.9 | 32.4 | 91.9 | 1.32× | 1.78× |
| print | 1983 | 1130 | – | 1.75× | 1.62× |
| stdout | 165 | 163 | – | 1.01× | 1.08× |
| csv | 250 | 302 | 510 | 0.83× | 0.88× |
| toml | 235 | 1349 | 277 | 0.17× | 0.21× |
| url | 372 | 784 | – | 0.47× | 0.61× |
| base64 | 123 | 189 | – | 0.65× | 0.69× |
| hex | 75.7 | 126 | – | 0.60× | 0.49× |
| html | 274 | 311 | – | 0.88× | 0.57× |
| zip | 266 | 450 | – | 0.59× | 0.36× |
| tar | 5.43 | 18.4 | – | 0.30× | 0.40× |
| sha256 | 293 | 291 | 357 | 1.01× | 0.95× |
| sha512 | 509 | 501 | 509 | 1.02× | 0.98× |
| sha1 | 299 | 284 | – | 1.05× | 0.81× |
| md5 | 1055 | 1050 | – | 1.00× | 0.81× |
| crc32 | 82.2 | 82.1 | – | 1.00× | 0.72× |
| hmac | 258 | 718 | 283 | 0.36× | 0.35× |
| aesgcm | 164 | 147 | 152 | 1.12× | 1.35× |
| chacha | 545 | 824 | 1558¹ | 0.66× | 0.88× |
| argon2 | 686 | 1158 | 717¹ | 0.59× | 0.42× |
| bcrypt | 747 | 769 | 744 | 0.97× | 1.03× |
| gzip | 1549 | 3364 | 18023 | 0.46× | 0.28× |
| bigint | 43.4 | 47.8 | 88.2 | 0.91× | 1.23× |
| modpow | 139 | 195 | 246 | 0.71× | 0.85× |
| time | 125 | 75.7 | 224 | 1.65× | 1.82× |
| template | 467 | 192 | 606 | 2.43× | 2.42× |
| semver | 820 | 1005 | 1218 | 0.82× | 0.81× |
| heap | 1243 | 899 | 1340 | 1.38× | 1.74× |
| sortedmap | 4971 | 1819 | 5575 | 2.73× | 4.56× |
| seq | 512 | 10.7 | 482 | 47.85× | 76.95× |

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
| template | 188 ± 1.99 | 32.9 ± 0.72 | n/a | 198 ± 0.50 | 5.71×³ |
| semver | 269 ± 2.34 | 160 ± 3.82 | 241 ± 1.99 | 291 ± 3.00 | 1.68× |
| heap | 371 ± 2.01 | 151 ± 0.62 | n/a | 373 ± 3.57 | 2.45× |
| sortedmap | 1221 ± 20.7 | 301 ± 2.70 | n/a | 1219 ± 17.0 | 4.06× |
| seq² | 130 ± 1.85 | 2.37 ± 0.00 | 4.16 ± 0.18 | 130 ± 4.46 | 54.85× |

¹ Before main's ChaCha20 and Argon2 on the vector unit (chacha.c,
argon2.c), argon2 on one thread.

What this pass changed, by benchmark:

- gzip: std/compress's own DEFLATE (core/src/std/flate.c) in place of
  miniz: levels 1 to 6 Go's compress/flate (its hash tables and match
  skipping), 7 to 9 zlib's lazy hash chains, Huffman lengths by
  package-merge; decoding through 11-bit tables and a 64-bit bit
  buffer, a symbol consumed by one shift, 16 bytes a copy. The bytes
  differ from miniz's (and from Go's and zlib's: each makes its own)
  but every stream is valid DEFLATE, and each level keeps its meaning.
  On the benchmark's 54 MB of text, levels 1 to 6 come within 0.3% of
  Go's sizes (level 4 2.6% smaller), levels 7, 8 and 9 0.4%, 1.5% and
  1.8% larger than Go's (level 9 1.6% larger than zlib's). A decode of
  it takes 159M cycles, Go's about four times that, macOS's zlib
  (1.2.12, Apple's build) 124M.
- regex: a lazy DFA (core/src/std/regex.c) finds the leftmost-first
  match's span, forward then backward, in time linear in the text; the
  Pike VM then runs over the span alone, for captures. Patterns the DFA
  can't take (over 10,000 instructions, a cache that keeps flushing)
  stay on the VM.
- bigint: carries in two chains, division by a reciprocal and by a
  single limb, squaring detected, decimal printing by a cached table of
  powers, and an Int operand without making a BigInt.
- csv, time, files, lines: fields located in C and repeated short
  values interned; RFC 3339 formatted in C; fs's readText and
  writeText native; lines read a megabyte at a time.
- url, base64, hex, html, zip, tar, sha1, md5, crc32 (new here): tables
  in place of branches, outputs sized before they're filled, url's
  query looked up without a map, zip's entries inflated straight into
  their bytes, SHA-1 on the CPU's SHA instructions, MD5 and SHA-1's
  rounds unrolled.

What's still slower than Go is what the compiled code does around the
library: concat, split and fmt make a string a piece (Go's substrings
share their bytes; Asuka's strings own theirs); lines spends its time
handing each line through a Stream, not reading (1.8 times Go's
instructions); time's Time is a struct of Asuka arithmetic and
allocations a call; print builds each line as a new string before
writing it (stdout, with fewer and longer lines, is even with Go); seq,
heap, sortedmap, template and semver are Asuka code (a closure call a
step, structs copied). sha1, md5 and crc32 are even with Go: each is
one dependency chain through the CPU's instructions, run with fewer
instructions than Go's.

³ After the table's run: faster than Go's (std/template, below).

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

## std/template: its data made a json.Value directly, a leaner renderer (2026-11-19)

std/template converts its data to a json.Value directly (json.c's
ak_json_value, by the value's descriptor: no JSON written and parsed
again, its strings shared), and renders by a stack of bindings that a
loop binds once and changes in place (its loop variable an index until
it's read whole), filters known as the template is parsed (and their
literal arguments' values), and small functions on the hot paths
(lang/LOWERING.md: a compiled function clears and releases all its
registers each call); float.fixed formats with ak_float_fixed, not
snprintf. Measured as above (load 30 to 50; seven interleaved runs,
cycles the median, which varied by about 10% for both, instructions the
median, within 0.5%; in millions, the setup, under 10 M, included):

| benchmark | cycles before | now | Go | now / Go | instructions before | now | Go |
|---|---:|---:|---:|---:|---:|---:|---:|
| template | 321 | 127 | 203 | 0.63× | 1,439 | 678 | 777 |

Of what's left, about a sixth is making the data's json.Value and
freeing it again (an object a struct, its map's index and arrays, a
Value a member), which text/template, reading the struct by
reflection, doesn't do.

## Calls, bounded ints, case mapping, SortedMap's own order (2026-11-19)

The machine shared (load 90 to 280): cycles, the fewest of five
interleaved runs, and instructions retired, the median, the whole
process, in millions; "before" is c43394e1. The benchmarks this pass
changed (the others' instructions are within 2% of before):

| benchmark | cycles before | now | Go | now / Go | instructions before | now | Go |
|---|---:|---:|---:|---:|---:|---:|---:|
| case | 2,172 | 1,262 | 1,621 | 0.78× | 8,314 | 4,846 | 7,839 |
| concat | 168 | 152 | 157 | 0.97× | 1,106 | 1,013 | 936 |
| split | 833 | 808 | 645 | 1.25× | 3,669 | 3,613 | 2,567 |
| heap | 641 | 554 | 925 | 0.60× | 2,081 | 1,824 | 2,371 |
| sortedmap | 2,856 | 2,588 | 1,870 | 1.38× | 4,015 | 2,847 | 2,352 |
| mapstr | 860 | 596 | 1,369 | 0.44× | 2,642 | 2,666 | 4,094 |

- case: lower, upper and foldCase copy what doesn't change a run at a
  time and map ASCII stretches in one loop; a case-insensitive contains
  or hasSuffix of ASCII compares bytes in place (it folded the whole
  text into an array of code points, 48 MB for the 12 MB text), and a
  suffix folds only the text's end.
- concat: a file's let (syllables) is read borrowed, not retained and
  released three times a string; byteCount is the string's length read
  in place.
- sortedmap: its order is optional, none the keys' own <: the B-tree's
  search compares in place, branch-free, in a loop that calls nothing
  (the closure a comparison made clang read the node's keys again after
  each call). An inlined parameter the body only reads isn't copied
  (each node retained twice less a level), and `n = n.kids[i]` reads
  into n (the temp it went through kept a second reference). Still
  behind Go's B-tree in cycles: each node is a struct of three arrays,
  each a header and a buffer, so a level is two more dependent loads
  (the keys' and the children's headers) than Go's two slices in the
  node, and the leaf's insert is two array inserts (keys, values) to
  Go's one of key-value pairs.
- split: still a string object a piece (pieces in slabs), where Go's
  substrings are two words in the slice: what's left is that
  representation, not the code around it.

Hello world (`asuka new --bin`, `asuka build`, macOS arm64): 886,400
bytes before, 886,416 now.

## SortedMap on a B-tree in C, split's pieces in batches (2026-11-20)

std/collections' SortedMap and SortedSet keep their entries in the
runtime's B-tree (core/src/rt/tree.c; lang/LOWERING.md, "B-trees"): a
node of up to 63 keys holds its keys, values and children in one
allocation (Go's google/btree node holds two slices; the Asuka node it
replaces was a struct of three arrays, each a header and a buffer), the
keys ordered in C by their kind (a branch-free search for ints, one
comparison a probe for strings) or their type's compare, a by closure
only when one's given; nodes are counted and path-copied when shared,
and a change takes the tree out of its map and changes it in place, as
an array's push does. split, lines and fields make a piece of up to 16
bytes as a header and one 16-byte copy (inline), count a slab's pieces
once as it's filled, and an array of strings lets go of a slab's
pieces together: each of those counts was a store the next one's load
waited for. A piece is still a string of its own: a view into the
string it was cut from would need its own header too (16 bytes; a word
is 5), and every reader of a string's bytes would follow a pointer.

sortedops is new: a map of 200,000 keys, each one's floor and
ceiling, a walk of it all, 2,000 ranges of about ten entries, and half
its keys removed (Go: DescendLessOrEqual, AscendGreaterOrEqual, Ascend,
AscendRange, Delete). The machine shared (load 60 to 160): cycles, the
median of three interleaved runs, and instructions retired, the median
of three, the work alone (`-instr`'s), in millions; "before" is
231ab00b.

| benchmark | cycles before | now | Go | now / Go | instructions before | now | Go |
|---|---:|---:|---:|---:|---:|---:|---:|
| sortedmap | 1,924 | 779 | 1,519 | 0.51× | 2,757 | 1,308 | 2,240 |
| sortedops | – | 221 | 354 | 0.63× | – | 640 | 792 |
| split | 397 | 306 | 465 | 0.66× | 2,053 | 1,346 | 1,909 |

The other string benchmarks' instructions are as they were (interp,
case, fmt, template, lines within 1%); the VM's benchmarks (bench/vm)
too, words 4% fewer. sortedmap's peak memory went from 48 to 39 MB (Go:
41); split's is as it was (191 MB to Go's 98: a piece is a string
object, 24 bytes for a word, and the three splits' arrays live to the
end of their statement). Hello world: 902,928 bytes before, 902,944
now.

## AES-GCM, BigInt's limbs in place, filter's appends (2026-11-20)

A quiet machine (load 4 to 7): `go run ./bench/std/run -instr -langs
asuka,go -runs 5`, the work alone: cycles, the median of five
interleaved runs, in millions, and instructions' ratio to Go's;
"before" is cf5a5709.

| benchmark | cycles before | now | Go | now / Go | instructions / Go before | now |
|---|---:|---:|---:|---:|---:|---:|
| aesgcm | 135 | 113 | 118 | 0.96× | 1.35× | 0.99× |
| bigint | 34.4 | 27.7 | 31.1 | 0.89× | 1.27× | 1.02× |
| modpow | 94.2 | 91.8 | 155 | 0.59× | 0.85× | 0.78× |
| seq | 59.4 | 59.3 | 58.2 | 1.02× | 1.19× | 1.13× |
| concat | 132 | 133 | 128 | 1.03× | 1.01× | 1.01× |
| time | 48.5 | 48.4 | 47.5 | 1.02× | 0.99× | 0.99× |
| sha512 | 491 | 498 | 490 | 1.02× | 0.98× | 0.97× |

- aesgcm (core/src/std/cryptohw.c): GHASH multiplies by H·x^-1, so a
  256-bit product needs no shift, and reduces it by two carry-less
  multiplies by 0xc2 << 56 (as Go's and Linux's ARMv8 GHASH), not
  Gueron's shifts: about 20 vector instructions fewer an eight blocks'
  reduction. A block's product is four word products by H and its
  words swapped (precomputed: Karatsuba's three would fold the block's
  words, two more instructions). The round keys sit at fixed indexes,
  so the rounds unroll; the last key and the data go in one EOR3; the
  counter is a vector added to; the powers of H are read in the loop
  rather than spilled. 338 instructions a 128 bytes, from 367 (Go's
  gcmAesEnc about 350). x86's path is the same steps in AES-NI and
  PCLMULQDQ (checked against arm64's and the old code's under Rosetta).
- bigint (core/src/std/bigint.c): a BigInt's bytes are a sign byte,
  three zero bytes and the magnitude's 64-bit little-endian words, which
  start 16 bytes into the object: the limbs themselves, aligned. An
  operand is read where it is and a result computed into the bytes that
  become it, allocated without clearing (ak_bytes_raw), so a times(i)
  is one pass and one allocation, as Go's in-place math/big. mul_1 and
  addmul_1 take eight limbs a turn (4.5 instructions a limb, Go's
  mulAddVWW 4.4); pow goes left to right (3^200000: squares and
  products by 3, half the instructions); radix 10's digits two at a
  time, the big base's reciprocal computed once a conversion. The
  factorial loop takes 6 M cycles to Go's 10 M; what's left in
  instructions is the loop's own (the struct, the call, an allocation a
  step where Go reuses its slice).
- seq: a filter's and a compactMap's appends into the array their loop
  made check only its room (arr.append_own). What's left: each eager
  step is a loop of its own with an array of its own, as Go's are, but
  every append stores its count and reads the array's buffer again
  (the element's store may alias them, and the loop's overflow checks
  exit before them, so clang can't keep them in registers), 3
  instructions an element Go's indexed store doesn't have. Cycles vary
  by 10% between passes here (the five arrays' 160 MB).
- time and sha512: even with Go (a second pass: 1.03× and 1.01×), at
  Go's instructions or fewer: time is its Asuka arithmetic, sha512 one
  dependency chain through the CPU's SHA-512 instructions. concat, at
  1.01× Go's instructions, took 1.03× and 1.09× its cycles in two
  passes: a string made and freed a turn, where Go's collector frees in
  batches; nothing in the instructions to take out.

The VM's benchmarks (bench/vm -instr) retire the instructions they did
(within 0.05%). Hello world (`asuka new --bin`, macOS arm64): 902,992
bytes.
