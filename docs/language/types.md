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
or interpolation; `"""…"""` spans lines (without its first line when
that's blank, and without the indentation of its closing quotes' line).
A value that isn't a number, a bool or a string shows as text: an enum's
case by its name, an optional as `none` or its value, an error as its
message, a color as it's written (`#0a84ff`, `.accent`), a duration in
milliseconds (`1500ms`), the rest as JSON (and what JSON can't hold, a
tuple or a function, as its type: `<(int, string)>`).

A tuple's elements are read by taking it apart: `let (count, label) =
pair`, `for (i, x) in xs.enumerated()`, or a tuple pattern in a `match`
([Control flow](control-flow.md#control-flow)).

A map's keys, and a set's elements, are ints, strings, bools, durations,
colors or enums without payloads (the type's `Hashable`).

## Operators

From the tightest to the loosest; the operators on a line bind alike,
and go left to right (`??` right to left):

| Operators | What they do |
|---|---|
| `!x`, `-x`, `~x` | not, negation, an int's bits flipped |
| `<<`, `>>` | an int's bits shifted left, and right (keeping its sign) |
| `*`, `/`, `%`, `&` | multiplication, division, remainder, bitwise and |
| `+`, `-`, <code>&#124;</code>, `^` | addition (and strings, arrays joined), subtraction, bitwise or, exclusive or |
| `..`, `..<` | ranges: `1..3`, `0..<n` |
| `==`, `!=`, `<`, `<=`, `>`, `>=` | comparisons (they don't chain) |
| `&&` | and |
| <code>&#124;&#124;</code> | or |
| `??` | an optional's value, or the default |

The bitwise operators take ints. A shift by 64 or more shifts every bit
out (`>>` of a negative gives -1); a shift by a negative count is an
error. With them and `b[i]` (a byte as an int), a program reads and
writes binary data, a Bluetooth characteristic or an NFC record:

```asuka
let raw = b[1] | b[2] << 8                         // a little-endian uint16
let celsius = if raw & 0x8000 != 0 { raw - 0x10000 } else { raw }
let out = bytes([flags, value & 0xff, value >> 8])
```

`int` arithmetic wraps: past the largest `int` (2⁶³ − 1), `+`, `-` and
`*` go on from the smallest, as 64-bit two's complement does
(`9223372036854775807 + 1` is `-9223372036854775808`). That's what
hashes, checksums and binary protocols want, and it costs no check on
every operation. Dividing by zero (`/`, `%`) throws; `int(x)` of a float
past the range gives the nearest `int` (`0` for NaN).

## Built-in methods

Strings, bytes, arrays, sets, maps and optionals have methods; those
that change their value (`push`, `insert`, `removeAt`, `removeWhere`,
`pop`, `clear`, `sort`, `remove`) take a `var` or state:

| Type | Methods |
|---|---|
| `[T]` | `count()`, `count(where)`, `isEmpty()`, `first()`, `last()`, `get(i)` (a `T?`), `push(x)`, `insert(x, at: i)`, `removeAt(i)`, `removeWhere(p)`, `pop()`, `clear()`, `map(f)`, `filter(p)`, `any(p)`, `all(p)`, `index(p)` (an `int?`), `find(p)`, `contains(x)`, `sum()`, `reversed()`, `slice(from, to)` (`to` left out: the end), `prefix(n)`, `suffix(n)`, `sorted()`, `sorted(by)`, `sort()`, `sort(by)`, `enumerated()`, `joined(separator:)` (of strings), `+` |
| `Set<T>` | `count()`, `isEmpty()`, `contains(x)`, `insert(x)` (whether it was new), `remove(x)` (whether it was there), `union(s)`, `intersection(s)`, `subtracting(s)`, `isSubset(of: s)`, `isSuperset(of: s)`; `Set(xs)` is an array's distinct elements, `Set<T>()` none |
| `[K: V]` | `m[k]` (a `V?`), `m[k] = v`, `count()`, `isEmpty()`, `has(k)`, `get(k)`, `remove(k)`, `keys()`, `values()` (in insertion order) |
| `string` | `count()` (characters), `byteCount()`, `isEmpty()`, `upper()`, `lower()`, `trim()`, `trimStart()`, `trimEnd()`, `trimPrefix(s)`, `trimSuffix(s)`, `contains(s)`, `hasPrefix(s)`, `hasSuffix(s)`, `replace(old, new, count:)` (every one unless count says), `split(sep, count:)`, `fields()` (the words), `lines()`, `find(s)`, `findLast(s)`, `findAny(chars)` (byte offsets, or none), `slice(from, to)` (characters), `byteSlice(from, to)` (bytes: the offsets find gives), `padStart(width, with:)`, `padEnd(width, with:)`, `toInt()`, `toFloat()`, `repeat(n)`, `chars()`, `bytes()`, `+` |
| `bytes` | `b[i]` (an `int`, 0 to 255; out of range is an error), `count()`, `isEmpty()`, `get(i)` (an `int?`), `slice(from, to)`, `concat(b)`, `string()` (UTF-8; what isn't becomes U+FFFD), `hex()` and `hex(separator)` (`"04 a1"`); `bytes()` is none, `bytes([0x04, 0xa1])` those, `s.bytes()` a string's UTF-8 |
| `int` | `float()`, `abs()`, `padded(width)` (`7.padded(2)` is `"07"`) |
| `float` | `fixed(digits)`, `rounded()`, `floor()`, `ceil()`, `int()` (toward zero), `abs()` |
| `Color` | `opacity(a)`: drawn at a times its opacity (a semantic color stays semantic); `hex()` (`"#0a84ff"`; a semantic color, its name: `".accent"`) |
| `Duration` | `nanoseconds()`, `milliseconds()` (ints), `seconds()`, `minutes()`, `hours()` (floats), `rounded(to:)`, `truncated(to:)`, `+`, `-`, `* n` (an int or a float: `1.5 * 1h`), `/ n`, `/ d` (another Duration: how many times it goes in, a float) |
| `Length` | `* n` (`width: 50% * 2`, a point length scaled) |
| `T?` | `isNone()`, `isSome()` |

`min`, `max`, `abs`, `float(n)` and `int(x)` are functions;
`std/math` has the rest (`math.sqrt`, `math.pow`, `math.pi`, ...).

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
