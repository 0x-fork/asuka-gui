# UI runtime benchmarks

Asuka benches (`bench "…" { measure { … } }`, docs/testing.md) that
drive apps on the fake host, compiled to C as a release build is:
js-framework-benchmark's operations on a 1,000- and a 10,000-row table
and a `List` (rows.asuka), signals (signals.asuka: a 500-level chain of
derived props, one state read by 1,000 rows, 1,000 states read by a row
each) and layout (layout.asuka: a screen of 400 cards, about 5,000
views). Each operation runs in a frame's handler (`onFrame`,
`app.frame`), as a tap's would, so its writes are one flush, and is
settled (laid out, frames applied) inside what's measured.

```sh
go run ./cmd/asuka test --bench bench/ui      # time, allocations, and per iteration what reached the host
bench/ui/instr.sh ./asuka [core]              # instructions retired per iteration (macOS)
bench/ui/run.sh 3 before=./old:old/core after=./asuka   # timed runs, interleaved, tabulated
node bench/ui/web_decode.mjs                  # the web host's batch decoder under node
```

Each bench's line also says what crossed to the host per iteration
(`host: batches, ops, bytes, measures; nodes laid out`): counts that a
loaded machine doesn't move. `instr.sh` measures instructions retired
(`/usr/bin/time -l`) by the bench runner at two iteration counts, so the
difference is one iteration's: wall-clock time on the machine these
were taken on (an Apple M5, 10 cores, macOS 27, load average between 20
and 250 from other work) varied by ±50–800% between runs, instructions
by under 1%.

## Before and after the performance pass

Instructions per iteration (`instr.sh`), the baseline being the commit
before the pass with only the counters added:

| bench | before | after | |
|---|---:|---:|---:|
| 1k states read by a row each, one changed | 2,552,968 | 54,691 | 46.7× |
| a screen of 400 cards: one text changed | 315,358 | 96,881 | 3.3× |
| a screen of 400 cards: every title changed | 9,594,948 | 3,083,442 | 3.1× |
| append 1k to 10k rows, remove them | 515,423,989 | 211,053,412 | 2.4× |
| one state read by 1k rows, changed | 17,626,764 | 7,970,346 | 2.2× |
| 1k states read by a row each, all changed in one batch | 18,139,303 | 8,224,381 | 2.2× |
| create 10k rows, clear | 1,725,338,119 | 807,888,881 | 2.1× |
| select a row of 1k | 1,941,273 | 1,390,127 | 1.4× |
| create 1k rows, clear | 89,460,983 | 73,748,938 | 1.2× |
| replace 1k rows | 89,670,881 | 73,896,213 | 1.2× |
| remove a row of 1k, put it back | 7,850,041 | 6,660,739 | 1.2× |
| a List of 10k rows: update every 10th | 6,247,214 | 5,332,982 | 1.2× |
| swap 2 of 1k rows | 3,591,776 | 3,100,127 | 1.2× |
| update every 10th of 1k rows | 5,055,673 | 4,719,619 | 1.1× |
| update every 10th of 10k rows | 51,363,217 | 48,004,715 | 1.1× |
| a screen of 400 cards: mount | 53,677,793 | 51,493,113 | 1.0× |
| a chain of 500 derived props, the top changed | 273,229 | 268,354 | 1.0× |
| a List of 10k rows: create, clear | 48,395,549 | 47,889,385 | 1.0× |
| a List of 10k rows: swap 2 | 3,809,068 | 3,810,053 | 1.0× |

What each change bought:

- **Relayout boundaries** (core/src/layout): a text that changes asks
  its node again what its parent asked it, and stops climbing where
  the answers are the same, instead of dropping every ancestor's cache
  (which re-ran the parent's flex over all its children). One label in
  a 1,000-row column: 2.55M → 55K instructions, 11 → 5 nodes laid out;
  the timed run went from 135 µs to 21 µs.
- **Linear keyed lists** (core/src/ui/control.c, element.c): new rows'
  native indices counted on from the first, removed rows unlinked in
  one pass: creating and clearing 10,000 rows 1.73G → 0.81G
  instructions (with the above).
- **No identical ListUpdate**: a label change in a 10,000-row `List`
  sent all 10,000 IDs (80 KB) for the host to diff; now nothing.
- **Fake host bookkeeping** (core/host/fake): its op log isn't kept
  while a bench measures (it was a quarter of what a UI bench timed),
  and a settle no longer scans every view: `asuka test` is faster for
  every app with many views.
- What remains per row made is the row's own code (its closures,
  strings, bindings: the VM and runtime's) and the fake host's
  bookkeeping; per label changed, the binding, the string, the
  measurement and, where the label wraps, the frames of every row
  below it (a non-virtualized column's).

## The web decoder

`node bench/ui/web_decode.mjs` (node 24, the same machine): a batch
making 1,000 rows (17,000 ops, 246 KB) decodes in ~640 µs, 38 ns an op;
1,000 label changes (string props) in ~140 µs, 140 ns an op, most of it
`TextDecoder.decode`. Hand-written ASCII fast paths for short strings
(char-by-char concatenation, `String.fromCharCode.apply`) measured
slower than node's `TextDecoder`, so the decoder is unchanged. The
Swift decoder reads through raw pointers with no allocation per op
beyond a string's; the Kotlin one now decodes a batch's strings through
one buffer instead of a `ByteArray` each.
