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
| chacha | 281 ± 2.33 | 164 ± 1.52 | n/a | 293 ± 3.38 | 1.72× |
| argon2 | 126 ± 1.83 | 53.0 ± 0.95 | n/a | 125 ± 0.81 | 2.38× |
| bcrypt | 176 ± 0.48 | 183 ± 0.37 | 171 ± 0.62 | 176 ± 0.25 | 0.96× |
| gzip | 3460 ± 4.95 | 673 ± 1.68 | 863 ± 5.68 | 3781 ± 7.39 | 5.15× |
| bigint | 20.5 ± 0.09 | 7.40 ± 0.02 | 14.7 ± 0.06 | 20.5 ± 0.05 | 2.77× |
| modpow | 46.4 ± 0.09 | 35.7 ± 0.05 | 33.3 ± 0.23 | 46.2 ± 0.15 | 1.30× |
| time | 138 ± 1.73 | 11.3 ± 0.10 | 66.9 ± 1.33 | 225 ± 1.75 | 12.22× |
| template | 188 ± 1.99 | 32.9 ± 0.72 | n/a | 198 ± 0.50 | 5.71× |
| semver | 269 ± 2.34 | 160 ± 3.82 | 241 ± 1.99 | 291 ± 3.00 | 1.68× |
| heap | 371 ± 2.01 | 151 ± 0.62 | n/a | 373 ± 3.57 | 2.45× |
| sortedmap | 1221 ± 20.7 | 301 ± 2.70 | n/a | 1219 ± 17.0 | 4.06× |
| seq | 130 ± 1.85 | 2.37 ± 0.00 | 4.16 ± 0.18 | 130 ± 4.46 | 54.85× |

¹ After the table's run: fs.read reads at the file's size now (aaae01aa);
three runs.

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

HTTP, 50 keep-alive connections, 5 s after 1 s of warm-up, the load
generator on the same machine:

| server | /hello req/s | p50 | p99 | /json req/s | p50 | p99 |
|---|---:|---:|---:|---:|---:|---:|
| Asuka (std/http) | 105,291 | 456 µs | 937 µs | 107,126 | 460 µs | 750 µs |
| Go (net/http) | 93,730 | 526 µs | 941 µs | 89,415 | 542 µs | 1.12 ms |
| Bun (Bun.serve) | 110,819 | 396 µs | 877 µs | 114,095 | 383 µs | 847 µs |
