# Toolchain benchmarks

```sh
go test ./bench/tools -bench . -benchmem          # asuka check's compile, in process, on the biggest packages
bench/tools/instr.sh 5 ./asuka-old ./asuka-new    # whole commands, instructions retired (median of 5)
ASUKA_CPUPROFILE=cpu.out asuka check std/base     # a command's CPU profile (ASUKA_MEMPROFILE: allocations)
go tool pprof -top asuka cpu.out
```

## Before and after the performance pass

Instructions retired by the whole command (median of 5, Apple M5;
`instr.sh`), the CLI before the pass and after:

| command | before | after | |
|---|---:|---:|---:|
| asuka version | 179M | 58M | 3.1× |
| asuka check examples/counter | 230M | 80M | 2.9× |
| asuka check std/base | 460M | 235M | 2.0× |
| asuka check std/template | 358M | 164M | 2.2× |
| asuka check examples/usage | 547M | 263M | 2.1× |
| asuka check examples/gallery | 469M | 239M | 2.0× |
| asuka fmt -l std/base | 231M | 106M | 2.2× |
| asuka test examples/counter | 1,000M | 138M | 7.2× |

`asuka test examples/counter` went from 85 ms to 45 ms of wall time
and from 0.28 s to 0.02 s of system time.

What each change bought:

- **Startup** (lang/diag): too-large's example program, 70,000 lines,
  was written with fmt.Fprintf as the package initialized, in every
  command: 9.6 MB, 140,000 allocations, ~10 ms. It's made when read.
- **Garbage collection** (cmd/asuka): a one-shot command's heap is tens
  of megabytes and collecting it as it went cost a third of a check;
  those commands collect only past 1 GiB (dev and lsp keep the
  default).
- **The object cache** (internal/cbuild): an unchanged build re-read
  and re-hashed ~600 core files, stat'ed each header once per object,
  read 300 manifests and rewrote 600 modification times. File hashes
  are kept across processes by size and modification time, a build
  hashes each file once, a set of a build's objects is found in one
  file, and entries are marked used hourly.
- **Compiling for every platform** (lang/compile): a program with
  platforms' files compiles them at once, sharing the files read;
  naming a std package's directory stats one file instead of globbing
  them all. The language server checks the other platforms at once.

In process (`go test ./bench/tools -bench Check`), a check of std/base
for every platform is about 25 ms and the counter's 5 ms; what remains
is parsing, checking and lowering, with the prelude parsed and checked
again by each compile.
