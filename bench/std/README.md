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

RESULTS
