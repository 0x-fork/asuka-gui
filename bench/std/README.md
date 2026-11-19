# The standard library's benchmarks

The same work in Asuka (`asuka/main.asuka`), Go (`go/main.go`) and Bun
(`bun/main.js`), on the same inputs: strings, maps and sets, sorting,
regex, files and stdout, CSV and TOML, URLs, Base64 and hex, HTML
escaping, zip and tar archives, hashes and checksums, crypto and
compression, bigint, time, template, semver, collections, Seq pipelines. Each program sets up
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
Asuka compiled to C (`asuka build`). The machine was shared and loaded
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
| interp | 531 | 1105 | 514 | 0.48× | 0.53× |
| concat | 771 | 164 | 805 | 4.70× | 4.64× |
| split | 1440 | 535 | 1454 | 2.69× | 3.27× |
| find | 438 | 651 | 467 | 0.67× | 0.45× |
| case | 1854 | 1510 | 1910 | 1.23× | 0.90× |
| count | ≈0 | 322 | ≈0 | ≈0 | ≈0 |
| num | 715 | 785 | 775 | 0.91× | 0.93× |
| fmt | 2313 | 1014 | 2315 | 2.28× | 2.26× |
| mapint | 590 | 496 | 658 | 1.19× | 0.86× |
| mapstr | 593 | 689 | 564 | 0.86× | 0.93× |
| set | 627 | 485 | 600 | 1.29× | 0.93× |
| sortint | 110 | 1233 | 94.2 | 0.09× | 0.26× |
| sortstr | 840 | 718 | 764 | 1.17× | 1.05× |
| sortby | 1247 | 6941 | 1254 | 0.18× | 0.10× |
| regex | 622 | 6673 | 4397 | 0.09× | 0.07× |
| files | 8.83 | 13.0 | 9.50 | 0.68× | 0.98× |
| lines | 64.9 | 32.4 | 91.9 | 2.00× | 2.85× |
| print | 2081 | 1117 | – | 1.86× | 1.70× |
| stdout | 203 | 161 | – | 1.26× | 1.50× |
| csv | 370 | 321 | 510 | 1.15× | 1.22× |
| toml | 258 | 1352 | 277 | 0.19× | 0.25× |
| url | 500 | 783 | – | 0.64× | 0.86× |
| base64 | 120 | 169 | – | 0.71× | 0.71× |
| hex | 86.3 | 141 | – | 0.61× | 0.49× |
| html | 268 | 311 | – | 0.86× | 0.57× |
| zip | 263 | 443 | – | 0.59× | 0.35× |
| tar | 4.55 | 14.3 | – | 0.32× | 0.45× |
| sha256 | 294 | 291 | 357 | 1.01× | 0.97× |
| sha512 | 499 | 504 | 509 | 0.99× | 0.97× |
| sha1 | 305 | 286 | – | 1.07× | 0.80× |
| md5 | 1052 | 1036 | – | 1.02× | 0.81× |
| crc32 | 83.5 | 83.0 | – | 1.01× | 0.72× |
| hmac | 274 | 711 | 283 | 0.39× | 0.41× |
| aesgcm | 139 | 147 | 152 | 0.95× | 1.22× |
| chacha | 413 | 775 | 1558¹ | 0.53× | 0.92× |
| argon2 | 657 | 1202 | 717¹ | 0.55× | 0.42× |
| bcrypt | 751 | 775 | 744 | 0.97× | 1.03× |
| gzip | 1536 | 3414 | 18023 | 0.45× | 0.27× |
| bigint | 43.1 | 52.9 | 88.2 | 0.81× | 1.30× |
| modpow | 152 | 226 | 246 | 0.67× | 0.85× |
| time | 148 | 64.2 | 224 | 2.31× | 2.36× |
| template | 504 | 204 | 606 | 2.47× | 2.95× |
| semver | 900 | 898 | 1218 | 1.00× | 1.12× |
| heap | 941 | 851 | 1340 | 1.11× | 1.76× |
| sortedmap | 5524 | 1907 | 5575 | 2.90× | 4.88× |
| seq | 455 | 10.8 | 482 | 42.13× | 76.08× |

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
library: concat, split, fmt and csv make a string a piece (Go's
substrings share their bytes; Asuka's strings own theirs: csv makes a
string a field, where Go's reader makes one a record and slices it);
lines spends its time handing each line through a Stream, not reading
(2.9 times Go's instructions); time's Time is a struct of Asuka
arithmetic and allocations a call; stdout and print build each line as
a new string before writing it; seq,
heap, sortedmap, template and semver are Asuka code (a closure call a
step, structs copied). sha1, md5 and crc32 are even with Go: each is
one dependency chain through the CPU's instructions, run with fewer
instructions than Go's.

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

HTTP, 50 keep-alive connections, 5 s after 1 s of warm-up, the load
generator on the same machine:

| server | /hello req/s | p50 | p99 | /json req/s | p50 | p99 |
|---|---:|---:|---:|---:|---:|---:|
| Asuka (std/http) | 105,291 | 456 µs | 937 µs | 107,126 | 460 µs | 750 µs |
| Go (net/http) | 93,730 | 526 µs | 941 µs | 89,415 | 542 µs | 1.12 ms |
| Bun (Bun.serve) | 110,819 | 396 µs | 877 µs | 114,095 | 383 µs | 847 µs |
