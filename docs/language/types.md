# Values and types

| Type | Values |
|---|---|
| `int` | 64-bit integers: `42`, `1_000`, `0xff`, `0b1010`, `0o17` |
| `float` | 64-bit floating point: `3.14`, `1e-3` |
| `bool` | `true`, `false` |
| `string` | UTF-8 text: `"Hello, {name}"` |
| `bytes` | raw bytes |
| `[T]` | arrays: `[1, 2, 3]`, `[]` |
| `[K: V]` | maps, kept in insertion order: `["a": 1]`, `[:]` |
| `Set<T>` | sets of distinct values, kept in insertion order: `["a", "b"]` where a set is expected |
| `(A, B)` | tuples: `(1, "one")` |
| `T?` | optionals: a `T`, or `none` |
| `Duration` | `300ms`, `5s`, `2min`, `1h` (and `us`, `ns`) |
| `Color` | `#7c3aed`, `#7c3aed80`, and the semantic colors: `.label`, `.accent`, ... |
| `Length` | `100` (points), `50%`, `.auto` |
| `fn(A) -> B` | functions and closures |

Strings take any expression between braces: `"{count} items"`, `"Total:
{price * qty}"`. `\{` is a brace; `r"…"` is a raw string with no escapes
or interpolation, and `r#"…"#` a raw string that may hold a `"`: it
ends at a `"` followed by as many `#`s as it starts with (`r#"say
"hi""#`, `r##"a "# b"##`). `"""…"""` spans lines (without its first
line when that's blank, and without the indentation of its closing
quotes' line) and holds `"` and `""` as they are; `r"""…"""` is a raw
one.
A value that isn't a number, a bool or a string shows as text: an enum's
case by its name, an optional as `none` or its value, an error as its
message, a color as it's written (`#0a84ff`, `.accent`), a duration in
milliseconds (`1500ms`), the rest as JSON. What JSON can't hold is
written in the same way: a tuple as its elements in parentheses, each as
an array's are (`(1,"a","red")`), and a map whose keys aren't strings
with each key as it's written (`{1:"a"}`); a function, which has no
text, as its type (`<fn(int) -> int>`).

An interpolation may say how it's written after a colon, as Python
and Rust do: `"{price:>8.2f}"`. The format is
`[[fill]align][sign][#][0][width][,|_][.precision][type]`: `<`, `>` and
`^` align left, right and centered in the width, after the fill
character (a space unless it's given); `+` writes a plus too; `0` fills
with zeros after the sign; `,` or `_` groups an int's digits (and a
float's whole part); `.2` is a float's digits after its point, or the
most characters of a text. The type is `x`, `X`, `o` or `b` for an
int's digits in hexadecimal, octal or binary (`#` writes `0x`, `0o`,
`0b` before them), or `f`, `e` and `%` for a number as a fixed point, an
exponent or a percentage. A number is right-aligned, anything else
left-aligned as its text, which takes a fill, an alignment, a width and
a precision. The compiler reads the format: one that doesn't fit its
value is an error where it's written
([format-spec](../errors/format-spec.md)).

```asuka check
fn row(name: string, count: int, share: float) -> string = "{name:<10}{count:>6,}{share:>8.1%}"

let price = "{1234.5:,.2f}"          // 1,234.50
let hex = "{255:#06x}"               // 0x00ff
let title = "{"Total":*^11}"         // ***Total***
let table = row("asuka", 12500, 0.4) // asuka     12,500   40.0%
```

A width counts characters (code points), not how wide a terminal draws
them.

A tuple's elements are read by their place, `pair.0` and `pair.1`, and
a `var`'s change in place (`pair.0 += 1`); a place the tuple doesn't
have (`pair.2` of a pair) is a compile error. A tuple is also taken
apart: `let (count, label) = pair`, `for (i, x) in xs.enumerated()`, or
a tuple pattern in a `match` ([Control flow](control-flow.md#control-flow)).

A map's keys, and a set's elements, are `Hashable`: ints, strings,
bools, bytes, durations, colors, and the enums, structs, tuples,
optionals, arrays, maps and sets made of them (a struct or an enum that
holds itself is when its other parts are; a map's keys and values both).
Floats aren't, since NaN isn't equal to itself, nor are functions:

```asuka
struct Cell {
  row: int
  col: int
}

let marks: [Cell: string] = [Cell(row: 0, col: 2): "x"]
let seen: Set<(int, int)> = [(0, 0), (1, 2)]
```

## Operators

From the tightest to the loosest; the operators on a line bind alike,
and go left to right (`??` right to left):

| Operators | What they do |
|---|---|
| `!x`, `-x`, `~x` | not, negation, an int's bits flipped |
| `<<`, `>>` | an int's bits shifted left, and right (keeping its sign) |
| `*`, `/`, `%`, `&`, `&*` | multiplication, division, remainder, bitwise and, wrapping multiplication |
| `+`, `-`, <code>&#124;</code>, `^`, `&+`, `&-` | addition (and strings, arrays joined), subtraction, bitwise or, exclusive or, wrapping addition and subtraction |
| `..`, `..<` | ranges: `1..3`, `0..<n` |
| `==`, `!=`, `<`, `<=`, `>`, `>=` | comparisons (they don't chain) |
| `&&` | and |
| <code>&#124;&#124;</code> | or |
| `??` | an optional's value, or the default |

`<`, `<=`, `>` and `>=` order ints, floats, strings, durations, enums
without payloads (by the order their cases are declared) and types
with a `compare` method; a struct or an enum takes the arithmetic
operators from its methods too, `add`, `sub`, `mul`, `div`, `rem` and
`neg` ([Declarations](declarations.md#structs)).

The bitwise operators take ints. A shift by 64 or more shifts every bit
out (`>>` of a negative gives -1); a shift by a negative count is an
error. With them and `b[i]` (a byte as an int), a program reads and
writes binary data, a Bluetooth characteristic or an NFC record:

```asuka
let raw = b[1] | b[2] << 8                         // a little-endian uint16
let celsius = if raw & 0x8000 != 0 { raw - 0x10000 } else { raw }
let out = bytes([flags, value & 0xff, value >> 8])
```

An `int` holds −2⁶³ to 2⁶³ − 1. `+`, `-` and `*` whose result is past
that range throw an error, "integer overflow", as dividing by zero (`/`,
`%`) does: a `catch` takes it, and when nothing does it goes to the
nearest `Boundary` ([Errors](errors.md#errors)). So do `-x` and `abs(x)`
of the smallest `int`, its `/ -1`, and an array's `sum()` past the
range. A count, a size or a price that overflows is a bug shown where it
happens, not a wrong number saved.

```asuka
let big = 9223372036854775807
let n = try big + 1 catch { 0 }               // 0: it threw
let next = big.addingChecked(1) ?? big          // none on overflow: big
```

For numbers past int's range, `std/bigint`'s `BigInt` is an integer of
any size, with the arithmetic operators and the comparisons
([Standard library](../stdlib.md)):

```asuka check
import std/bigint.{BigInt}

let big = BigInt.of(9223372036854775807)
let past = big * big + BigInt.one           // 85070591730234615847396907784232501250
let digits = "{BigInt.factorial(30)}"       // 265252859812191058636308480000000
```

`&+`, `&-` and `&*` wrap instead: past the largest `int` they go on from
the smallest, as 64-bit two's complement does (`9223372036854775807 &+
1` is `-9223372036854775808`). That's what hashes, checksums and binary
protocols want; `&+=`, `&-=` and `&*=` assign them:

```asuka
fn fnv1a(data: bytes) -> int {
  var h = 0xcbf2_9ce4_8422_2325
  for i in 0..<data.count() {
    h ^= data[i]
    h &*= 0x100_0000_01b3
  }
  return h
}
```

`a.addingChecked(b)`, `subtractingChecked` and `multipliedChecked` give
an `int?`, none on overflow, for code that handles it as a value. The
shifts and the bitwise operators never throw for a result; `int(x)` of
a float past the range gives the nearest `int` (`0` for NaN).

## Built-in methods

Strings, bytes, arrays, sets, maps and optionals have methods; those
that change their value (`push`, `pushFirst`, `insert`, `removeAt`,
`removeWhere`, `pop`, `popFirst`, `clear`, `sort`, `remove`) take a
`var` or state:

| Type | Methods |
|---|---|
| `[T]` | `count()`, `count(where)`, `isEmpty()`, `first()`, `last()`, `get(i)` (a `T?`), `push(x)`, `pushFirst(x)`, `insert(x, at: i)`, `removeAt(i)`, `removeWhere(p)`, `pop()`, `popFirst()`, `clear()`, `map(f)`, `flatMap(f)` (the arrays `f` gives, one after another), `compactMap(f)` (what `f` gives, without its nones), `filter(p)`, `reduce(initial, f)`, `any(p)`, `all(p)`, `index(p)` (an `int?`), `find(p)`, `contains(x)`, `sum()`, `min()`, `max()` (a `T?`; `min(by:)`, `max(by:)` with a less-than, `min(key:)`, `max(key:)` by a key), `unique()`, `grouped(by: f)` (a `[K: [T]]`), `chunked(n)`, `zip(other)` (pairs, as long as the shorter), `reversed()`, `slice(from, to)` (`to` left out: the end), `prefix(n)`, `suffix(n)`, `sorted()`, `sorted(by)`, `sorted(key:, descending:)`, `sort()`, `sort(by)`, `sort(key:, descending:)`, `enumerated()`, `joined(separator:)` (of strings), `lazy()` (a `Seq<T>`), `+` |
| `Set<T>` | `count()`, `isEmpty()`, `contains(x)`, `insert(x)` (whether it was new), `remove(x)` (whether it was there), `union(s)`, `intersection(s)`, `subtracting(s)`, `isSubset(of: s)`, `isSuperset(of: s)`, `lazy()`; `Set(xs)` is an array's distinct elements, `Set<T>()` none |
| `[K: V]` | `m[k]` (a `V?`), `m[k] = v`, `count()`, `isEmpty()`, `has(k)`, `get(k)`, `remove(k)`, `keys()`, `values()` (in insertion order), `mapValues(f)`, `filter((k, v) => …)`, `lazy()` (its `(key, value)` pairs) |
| `string` | `count()` (characters), `byteCount()`, `isEmpty()`, `upper()`, `lower()`, `trim()`, `trimStart()`, `trimEnd()`, `trimPrefix(s)`, `trimSuffix(s)`, `contains(s)`, `hasPrefix(s)`, `hasSuffix(s)`, `replace(old, new, count:)` (every one unless count says), `split(sep, count:)`, `fields()` (the words), `lines()`, `find(s)`, `findLast(s)`, `findAny(chars)` (byte offsets, or none), `slice(from, to)` (characters), `byteSlice(from, to)` (bytes: the offsets find gives), `padStart(width, with:)`, `padEnd(width, with:)`, `toInt()`, `toInt(radix:)`, `toFloat()`, `repeat(n)`, `chars()`, `lazy()` (its characters, a `Seq<string>`), `reversed()`, `bytes()`, `+` |
| `bytes` | `b[i]` (an `int`, 0 to 255; out of range is an error), `count()`, `isEmpty()`, `get(i)` (an `int?`), `slice(from, to)`, `concat(b)`, `string()` (UTF-8; what isn't becomes U+FFFD), `hex()` and `hex(separator)` (`"04 a1"`); `bytes()` is none, `bytes([0x04, 0xa1])` those, `s.bytes()` a string's UTF-8 |
| `int` | `float()`, `abs()`, `toString(radix:)` (`255.toString(radix: 16)` is `"ff"`), `padded(width)` (`7.padded(2)` is `"07"`), `addingChecked(o)`, `subtractingChecked(o)`, `multipliedChecked(o)` (an `int?`: none on overflow) |
| `float` | `fixed(digits)`, `rounded()`, `floor()`, `ceil()`, `int()` (toward zero), `abs()` |
| `Color` | `opacity(a)`: drawn at a times its opacity (a semantic color stays semantic); `hex()` (`"#0a84ff"`; a semantic color, its name: `".accent"`) |
| `Duration` | `nanoseconds()`, `milliseconds()` (ints), `seconds()`, `minutes()`, `hours()` (floats), `rounded(to:)`, `truncated(to:)`, `+`, `-`, `* n` (an int or a float: `1.5 * 1h`), `/ n`, `/ d` (another Duration: how many times it goes in, a float) |
| `Length` | `* n` (`width: 50% * 2`, a point length scaled) |
| `T?` | `isNone()`, `isSome()` |

The methods that call a function you give them (`map`, `filter`,
`reduce`, `sorted(by:)`, `grouped(by:)`, a map's `mapValues`...) take
one that throws, and then throw what it throws: the call is a `try`.
Without a throwing function, they don't throw.

```asuka
fn port(text: string) throws -> int = text.toInt() ?? throw Error("not a port: {text}")

fn ports(lines: [string]) throws -> [int] = try lines.map(l => try port(l))
```

A few of them, at work:

```asuka check
struct Sale {
  region: string
  amount: int
}

fn report(sales: [Sale]) -> [string] {
  let byRegion = sales.grouped(by: s => s.region)
  let totals = byRegion.mapValues(ss => ss.map(s => s.amount).sum())
  let best = sales.max(by: (a, b) => a.amount < b.amount)
  let codes = ["7", "x", "42"].compactMap(c => c.toInt())    // [7, 42]
  let pages = sales.chunked(50)                               // [[Sale]], 50 each
  var out = ["{best?.region ?? "none"} sold the most"]
  for (region, total) in totals { out.push("{region}: {total}") }
  for (i, page) in pages.enumerated() { out.push("page {i + 1}: {page.count()}") }
  out.push("{codes.reduce(0, (sum, c) => sum + c)}")
  return out
}
```

`unique()` keeps each element once, where it first comes, and
`grouped(by:)` keeps the keys in the order they first come: both need
`Hashable` values (the keys, for `grouped`). `min()` and `max()` order
as `sorted()` does, and give the first of equal elements. `chunked(n)`
of an `n` under 1 is an error, as `xs[i]` out of range is.

`sorted(key:)` orders the elements by what a function gives each, a
`Comparable` computed once per element: `people.sorted(key: p =>
p.age)`. `descending: true` puts the greatest first, and either way the
sort is stable: elements with equal keys keep their order. `sort(key:)`
sorts a `var` in place, and `min(key:)` and `max(key:)` give the element
whose key is least or greatest (the first of equal ones). `sorted(by:)`
takes a less-than instead, for an order no single key says.

```asuka check
struct Player {
  name: string
  score: int
  joined: int
}

fn ranking(players: [Player]) -> [string] {
  let best = players.sorted(key: p => p.score, descending: true)
  let newest = players.max(key: p => p.joined)
  return best.map(p => p.name) + [newest?.name ?? ""]
}
```

An array is a double-ended queue too: `pushFirst(x)` and `popFirst()`
change its start as `push(x)` and `pop()` change its end, each in O(1)
(amortized: an array keeps room at its start once it's changed there),
and so do `insert(x, at: 0)` and `removeAt(0)`. An insertion or a
removal elsewhere moves the elements on its shorter side. A queue is
an array: `queue.push(job)`, then `queue.popFirst()`.

`toInt(radix:)` reads a whole string in a radix from 2 to 36, letters
in either case and an optional sign first (`"ff".toInt(radix: 16)` is
255, `"-101".toInt(radix: 2)` is -5), or gives none; `toString(radix:)`
writes one, lowercase. A radix outside 2 to 36 is an error.

`min`, `max`, `abs`, `float(n)` and `int(x)` are functions;
`std/math` has the rest (`math.sqrt`, `math.pow`, `math.pi`, ...).

`toInt()` and `toFloat()` read the whole string, or give none: an
optional sign, then the number, with nothing around it (`" 12"` is
none: `trim()` it first). `toInt()` takes digits (`"+7"`, `"-42"`);
`toFloat()` also a fraction and an exponent (`"2.5"`, `".5"`,
`"1e-3"`) and what a float prints as (`"Infinity"`, `"NaN"`). Both
read `.` as the decimal point in every language.

## Lazy sequences

`xs.lazy()` is an array's elements as a `Seq<T>`, a lazy sequence. Its
`map`, `filter`, `take` and the rest make another `Seq` and compute
nothing; what reads it (a `for`, `toArray()`, `first()`, `reduce`...)
computes its elements one at a time, each through every step, with no
array in between. A chain over a million elements that keeps three
computes those three and what it took to find them, and a sequence may
have no end:

```asuka check
fn squaresUnder(limit: int) -> [int] = Seq(from: 1).map(n => n * n).takeWhile(n => n < limit).toArray()

fn firstLong(lines: [string]) -> string? = lines.lazy().map(l => l.trim()).find(where: l => l.count() > 80)

fn fibonacci() -> Seq<int> = Seq(state: (0, 1), next: s => (s.0, (s.1, s.0 + s.1)))
```

A map's `lazy()` gives its `(key, value)` pairs, a set's its elements, a
string's its characters, and a range's (`(0..<n).lazy()`) its numbers,
without an array of them. `Seq(…)` makes one from a function:

| Made with | Its elements |
|---|---|
| `Seq(from: n, by: step)` | `n`, `n + step`, ... without end; `by` is 1 unless it says |
| `Seq(first: x, next: f)` | `x`, `f(x)`, `f(f(x))`, ... until `f` gives none |
| `Seq(state: s, next: f)` | `f(s)` gives the next element and the next state, `(x, s2)`, or none at the end |
| `Seq(repeating: x, count: n)` | `x`, `n` times; without `count`, without end |

| | Methods |
|---|---|
| A new `Seq` | `map(f)`, `filter(p)`, `compactMap(f)`, `flatMap(f)` (the elements of the `Seq`s `f` gives, one after another), `enumerated()`, `zip(other)`, `take(n)`, `skip(n)`, `takeWhile(p)`, `skipWhile(p)`, `chunked(n)`, `windows(n)` (each `n` in a row: `[1, 2, 3]`'s `windows(2)` is `[1, 2]`, `[2, 3]`), `onEach(f)` (`f` called with each element as it's read), `unique()`, `concat(other)` |
| Reading it | `toArray()`, `toSet()`, `toMap()` (of `(key, value)` pairs), `count()`, `count(where:)`, `first()`, `last()`, `find(where:)`, `any(where:)`, `all(where:)`, `contains(x)`, `reduce(initial, f)`, `sum()`, `min()`, `max()`, `min(by:)`, `max(by:)`, `min(key:)`, `max(key:)`, `joined(separator:)` |

`take`, `find`, `first`, `any` and `all` stop pulling elements once
they have their answer: what comes after is never computed. A `Seq` is
read from its start each time (a `for` over it twice computes it
twice). It holds its source's value as it was when it was made, as any
value would: a change to the array after it doesn't change it.

A step's function runs later, as the sequence is read, so it can't
throw: do that in a `for`, or with an array's `map`. A function that
reading calls at once (`reduce`, `find(where:)`, `any`, `count(where:)`,
`min(key:)`...) may, and then the call throws what it throws, as an
array's methods do.

A priority queue, and maps and sets kept in their keys' order, are
`std/collections`' `Heap`, `SortedMap` and `SortedSet` ([Standard
library](../stdlib.md)).

## Value semantics

Strings, arrays, sets, maps, structs and enums are **values**: assigning one
or passing it gives a copy, as far as you can tell. Copies are cheap: they
share memory until one of them changes, and a value only one place holds
is changed in place.

```asuka
var a = [1, 2, 3]
var b = a
b.push(4)        // a is still [1, 2, 3]
```

Stores, closures, tasks and views are **references**: copies are the same
thing.

There is no null. A value that may be missing is an optional:

```asuka
let user: User? = none
if let u = user { Text(u.name) }
let name = user?.name ?? "Guest"
let first = items.first()!        // a crash if it's none: say so only when it can't be
```

After `?.`, the rest of the chain stops at none too: `user?.boss.name`
is a `string?`.

Optionals don't nest: a value is there or it isn't. `int??` is an error
(one `?` is enough), and a generic `T?` whose `T` is already optional
is that `T`.
