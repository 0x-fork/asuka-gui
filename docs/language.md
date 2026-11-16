# The Asuka language

Asuka apps are written in Asuka, a small language made for interfaces
that use each platform's own widgets. It's statically typed, compiles
ahead of time to C for release builds, and runs on a VM during
development so a save shows up in milliseconds, with every value and
every native view kept.

```asuka
component Counter {
  state count = 0

  SafeArea {
    justify: .center; align: .center; gap: 16; padding: 24

    Text("Count: {count}") { font: .largeTitle.bold() }
    Row {
      gap: 12
      Button("Reset") { variant: .gray; on press { count = 0 } }
      Button("Add") { variant: .filled; on press { count += 1 } }
    }
    if count >= 10 {
      Text("That's a lot") { color: .secondaryLabel }
    }
  }
}

app { Counter() }
```

This page covers the whole language. The elements themselves (`Text`,
`Button`, `List`, `Stack`, ...) have their own page:
[Components](components.md).

## Files, packages and imports

A source file ends in `.asuka`. A directory of them is a **package**:
its files see each other's declarations without importing anything.
`pub` makes a declaration visible to other packages.

```asuka
import asuka/http                 // the standard library
import @acme/charts               // a dependency (asuka add @acme/charts)
import ./components               // a package beside this one
import ./model.{Message, Folder}  // names used without the package's name
import ./theme as t               // a shorter name
```

A package's names are used through it: `http.get(url)`,
`components.Card("Hi")`. A package is named by its path's last segment;
one whose name isn't a name (`@acme/line-charts`) is imported with
`as`. The prelude (the built-in types, elements and functions) needs no
import. [The standard library](stdlib.md) lists the `asuka/...`
packages.

Statements and declarations end at the end of a line; `;` puts several
on one. A line that ends with an operator, a comma or an opening bracket
goes on to the next.

Comments are `// …` and `/* … */`. A `///` comment documents the
declaration under it, and editors show it.

## Values and types

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
message, the rest as JSON.

A map's keys, and a set's elements, are ints, strings, bools or enums
without payloads (the type's `Hashable`).

### Operators

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

### Built-in methods

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
`asuka/math` has the rest (`math.sqrt`, `math.pow`, `math.pi`, ...).

### Value semantics

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

### Declarations

```asuka
let pi = 3.14159             // a constant
var total = 0                // a variable (in functions and handlers)
state count = 0              // state: a value the UI follows
```

Types are inferred; write one where it helps: `let ids: [int] = []`.

`type` names a type, at the top of a file: `type Pair<A, B> = (A, B)`,
`type Check = fn(string) -> bool`. It's another name for the same type
(an alias): a `Pair<int, string>` is an `(int, string)`.

### Structs

```asuka
struct Message {
  id: string
  subject: string
  read: bool = false

  fn preview() -> string = subject.slice(0, 40)
}

let m = Message(id: "1", subject: "Hello")
let n = Message(id: m.id, subject: m.subject, read: true)
```

A struct is made by calling it with its fields, by name or in order;
fields with a default may be left out. Its methods see its fields by
their names; they can't change it (it's a value): one that would
returns a new one. `let` in a struct's body is a constant of the
type (`Font.title`), which `.title` names wherever a `Font` is
expected. `static fn` in a struct's or an enum's body is a function of
the type's, with no `self`: called on the type with named arguments and
defaults as any function (`Money.of(dollars: 2)`), or as `.of(…)`
wherever the type is expected.

```asuka
struct Money {
  cents: int

  static fn of(dollars: int, cents: int = 0) -> Money = Money(cents: dollars * 100 + cents)
}

let price: Money = .of(dollars: 4, cents: 50)
```

A struct may take type parameters, inferred where it's made:

```asuka
struct Page<T> {
  items: [T]
  next: string? = none
}

let page = Page(items: [1, 2, 3])     // a Page<int>
```

A struct pattern matches a struct's fields by their names, in `match`
and `if let`: `y` alone binds the field `y`, `y: py` binds it as `py`,
`x: 0` matches a value, and the fields it leaves out match anything.
Patterns nest:

```asuka
match line {
  Line(from: Point(x: 0, y: 0), dashed: true) => "dashed, from the origin"
  Line(from: Point(x: 0, y), to) => "from {y} on the y axis to {to.x}"
  Line(dashed) => "a line"
}
```

A struct takes operators from its methods: `fn add(o: T) -> R` is `a +
b`, `fn sub(o: T) -> R` (or `minus`) is `a - b`, and `fn compare(o: T)
-> int` (negative, zero or positive) is `<`, `<=`, `>` and `>=`. That's
how `asuka/time`'s `Time` adds a `Duration` and subtracts another `Time`:

```asuka
struct Money {
  cents: int

  fn add(o: Money) -> Money = Money(cents: cents + o.cents)
  fn compare(o: Money) -> int = cents - o.cents
}

let total = Money(cents: 250) + Money(cents: 99)
let over = total > Money(cents: 300)
```

### Enums

```asuka
enum Filter {
  all
  unread
  from(Person)

  fn matches(m: Message) -> bool = match self {
    .all => true
    .unread => !m.read
    .from(p) => m.from == p
  }
}

let f: Filter = .unread
```

Where the type is known, a case is written `.name`. `match` must cover
every case (or end with `_ =>`); patterns bind payloads (`.from(p)`, by
their labels too: `.server(code: c)`), match literals (`0`, `"x"`),
several cases (`.a | .b`), nest (`.from(Person(vip: true))`) and take
guards (`.from(p) if p.vip =>`). The compiler follows nested patterns
when it checks that a match covers everything, and says what's missing
as patterns: `match doesn't cover .key(_, false)`. A case's labeled payload with a zero value
(`false`, `0`, `""`, an empty array or map, `none`, an enum's first case)
may be left out where it's made: `.button(title: "Copy", run: copy)`
leaves `Action.button`'s `icon`, `role`, `disabled` and `checked` so.

A case's payload may hold the enum itself, so an enum can be a tree:

```asuka
enum Expr {
  num(int)
  add(Expr, Expr)

  fn eval() -> int = match self {
    .num(n) => n
    .add(a, b) => a.eval() + b.eval()
  }
}
```

### Functions and closures

```asuka
fn area(width: float, height: float = 1) -> float {
  return width * height
}

fn double(x: int) -> int = x * 2      // one expression

area(3, height: 4)
let squares = [1, 2, 3].map(x => x * x)
let sorted = people.sorted((a, b) => a.age < b.age)
```

Arguments are positional, then by name. A closure is `x => expr`, `(a,
b) => expr` or `x => { statements }`. It shares the variables it
captures with the code around it: what one changes, the other sees.

```asuka
var n = 0
let inc = () => { n += 1 }
inc()
inc()                // n is 2
```

State is shared the same way. Only `spawn` copies what it uses (values
only, as they are when it starts), since it runs on another thread.

A `fn` may be declared in a function's, a handler's, a closure's or a
method's body too. It sees what's around it as a closure does, and it
may call itself and the fns declared next to it in its block (and they
it):

```asuka
fn total(root: Node) -> int {
  var sum = 0
  fn walk(n: Node) {
    sum += n.size
    for c in n.children { walk(c) }
  }
  walk(root)
  return sum
}
```

It's a value like any function (`items.map(walk)`). Its arguments go
in order, without labels or defaults, and it isn't generic.

That's how a function calls itself. A closure kept in a var it calls
itself through (`var walk = …; walk = n => { … walk(c) … }`) holds the
var, and the var holds it: neither is ever freed, since nothing
collects such a cycle, and the compiler warns of it. A local fn calls
itself without holding itself.

### Interfaces and generics

An interface is a set of methods and fields; a type has it if it has
them (nothing to declare).

```asuka
interface Shape { fn area() -> float }

fn total<T>(items: [T], size: fn(T) -> float) -> float {
  var sum = 0.0
  for i in items { sum += size(i) }
  return sum
}
```

An interface is a type too: a value of one is any value that has its
methods, and calling one calls that value's.

```asuka
let shapes: [Shape] = [Circle(radius: 1), Square(side: 2)]
let areas = shapes.map(s => s.area())
```

Generic functions and types take type parameters. A parameter's
constraints are the program's interfaces and `Equatable`, `Comparable`
and `Hashable` (structs and enums have those when their fields do),
several joined by `&`. What a type argument must have, its values in
the generic code have: the interfaces' methods and fields, called
directly on each type it's compiled for.

```asuka
fn sortByArea<T: Shape>(xs: [T]) -> [T] = xs.sorted((a, b) => a.area() < b.area())

fn largestOnce<T: Shape & Hashable>(xs: [T]) -> [T] { … }

struct Labeled<T: Shape> {
  item: T
  fn label() -> string = "{item.area()}"
}
```

A type argument has an interface when it has its methods and fields: a
struct or an enum (an enum without payloads too), an interface asking
for them, or another type parameter constrained so. `T: Error` reads an
error's `message`.

### Control flow

`if` and `match` are values too. As one, an `if` needs an `else`, and a
block's value is its last expression:

```asuka
let label = if unread > 0 { "{unread} new" } else { "Inbox" }
let icon = match (online, unread) {
  (false, _) => "wifi.slash"
  (true, 0) => "tray"
  _ => "tray.full"
}
```

A tuple pattern (`(true, 0)`) matches each element with a pattern of
its own.

`for` goes over an array, a map's `(key, value)` pairs, or a range:
`a..<b` is the ints from `a` up to `b`, without it, and `a..b` with it
(`0..<3` is `[0, 1, 2]`, and a `for` over a range makes no array).
`while cond { … }` loops while cond holds; `break` and `continue` leave
the loop, or go on to its next turn.

`if let` and `while let` take an optional's value, or match a pattern,
and run their block when there's one: `if let u = user { … }`, `while
let job = queue.pop() { … }`, `if let .ready(v) = state { … }`. Their
condition may be a chain: lets and bools, separated by commas, in any
order. Each sees what the ones before it bound, and the block runs
when all of them hold (they're tried in order, up to the first that
doesn't):

```asuka
if let user = session.user, user.isAdmin, let team = teams[user.teamId] {
  Text("{team.name}'s admin")
} else {
  Text("Not an admin")
}
```

`let … else` binds a value or leaves. Its `else` block runs when the
optional is none (or the pattern doesn't match), and must leave:
`return`, `throw`, `break`, `continue` or `panic(…)` (the compiler says
so when it can go on). After it, the name holds the value, not an
optional:

```asuka
fn summary(id: string) -> string {
  let message = cache[id] else { return "(deleted)" }
  let .ready(body) = message.body else { return message.subject }
  return "{message.subject}: {body}"
}
```

`throw`, `return`, `break` and `continue` may go where a value does:
they leave instead of giving one (their type is `never`, which fits
any). After `??`, they say what to do with none:

```asuka
let user = cache[id] ?? throw MailError.missing(id)
for line in lines {
  let n = line.toInt() ?? continue
  total += n
}
```

They go likewise in an `if`'s or a `match` arm's branch, and as a
closure's body: `x => x ?? throw Error("empty")`.

## Components

A component is a piece of interface, declared with its parameters in
its header and everything else in its body:

```asuka
component MessageRow(message: Message) {
  prop compact = false            // a named prop, with its default
  prop accessory: View? = none    // a slot: a view the caller passes
  event archive                   // something it reports
  state expanded = false          // its own state

  Row {
    gap: 12; padding: [12, 16]
    Avatar(message.from)
    Column {
      grow: 1
      Text(message.subject) { font: .headline; lines: 1 }
      if !compact { Text(message.preview()) { color: .secondaryLabel } }
    }
    accessory
  }
  contextMenu {
    Button("Archive") { role: .destructive; on press { emit archive } }
  }
}
```

Using it looks the same as using a built-in element. Header parameters
go in the parentheses. Everything else goes in the block:

- props: `name: value`
- handlers: `on name { … }`
- two-way bindings: `bind name: target`
- styles: `style name`
- children

```asuka
MessageRow(m) {
  compact: true
  accessory: Badge(3)
  on archive { mailbox.archive(m.id) }
}
```

Each thing has one place: a header parameter can't be set in the block,
and a prop can't be passed in the parentheses.

A component that takes children declares `prop children: View`; what's
in its caller's block goes there. A named slot is `name { … }` in the
block (`toolbar { … }`).

`emit name(args)` raises an event; the caller's `on name(args) { }`
handles it. An event nobody handles does nothing; its name read as a
value is whether the caller handles it (`if click { … }`: a tray icon
whose click opens its menu unless the caller takes it).

A component that shows one element (its body's views are that one
element, or another component that does) takes the style props too:
what its caller gives goes on that element, over what the component
sets there.

```asuka
component Card(title: string) {
  Column {
    padding: 16; radius: 12; background: .secondaryBackground
    Text(title)
  }
}

Card("Plan") { padding: 8; width: 240 }   // padding 8, radius 12
```

### A component's body is declarations

A component's body is **not** code that runs once and is forgotten.
Each line declares something that stays true:

- `state x = …` is state the component owns.
- `let y = …` is derived from what it reads, and kept up to date
  (computed only when read, and only again when what it read changed).
- Every prop value of every element is kept up to date the same way:
  `Text("Count: {count}")` changes when `count` does, and nothing else
  does.
- `if`, `for` and `match` show what their condition says, now and
  later.

When `count` changes, the only thing that runs again is what read it:
that `Text`'s text, and the `if`. No component runs again, and the
platform's views are touched only where something changed.

### Handlers and statements

Code runs in handlers (`on press { … }`), functions, `task` and `effect`
blocks, and tests. There, reading state gives its current value, and
writing it updates what reads it:

```asuka
on press {
  count += 1
  todos.push(Todo(title: draft))
  todos[0].done = true      // writes todos: only the rows that read todos[0] update
  draft = ""
}
```

Writes in a handler show together when it ends (or when it reaches an
`await`).

Statements: `let`, `var`, `let … else`, assignments (`=`, `+= -= *= /=
%=`, `&= |= ^= <<= >>=`, `??=`), `if` and `if let`, `for x in xs` (an
array, a set), `for (k, v) in m`, `for (i, x) in xs.enumerated()`,
`while` and `while let`, `match`, `return`, `break`, `continue`,
`throw`, `do { } catch { }`, `defer { }`, `emit`
([Control flow](#control-flow)), and `fn` declarations ([Functions and
closures](#functions-and-closures)).

### Two-way bindings

Controls show a value and change it. `bind` passes them state to do both:

```asuka
state query = ""
state notify = true

TextField { bind text: query; placeholder: "Search" }
Toggle { bind on: notify; title: "Notifications" }
```

Without `bind`, a control only reports: `TextField { text: name; on
change(t) { name = t.trim() } }`.

A component's prop can be bound the same way when the component reports
it with `event <prop>Change`: `bind value: when` on a component with
`prop value` and `event valueChange(value: T)` passes the state in, and
writes it as the component emits the change:

```asuka
component Rating {
  prop value = 0
  event valueChange(value: int)

  Stepper {
    value: float(value)
    on change(v) { emit valueChange(int(v)) }
  }
}

component Review {
  state stars = 3

  Rating { bind value: stars }
}
```

### Lists of views

```asuka
for todo in todos key todo.id {
  TodoRow(todo) { on delete { todos.removeWhere(t => t.id == todo.id) } }
}
```

`for` keeps a row per key. A row whose item changed updates its own
bindings. A new item makes a row, and a removed one disposes its row. A
row that moved is moved, not rebuilt. Directly under a `List`, rows exist
only while they're on screen, so a list of 100,000 costs what's
visible.

### Stores

A store is state with the functions that change it, shared by the views
that use it:

```asuka
pub store Mailbox {
  state messages: [Message] = []
  let unread = messages.count(m => !m.read)

  pub fn markRead(id: string) {
    if let i = messages.index(m => m.id == id) { messages[i].read = true }
  }

  pub async fn refresh() throws {
    messages = try await http.get("https://example.com/inbox").json()
  }
}
```

A store is made like a struct (`Mailbox()`) and belongs to the component
or store that made it. `provide` makes it available to everything below,
and `use` gets it:

```asuka
app {
  provide Mailbox()
  Stack { Inbox() }
}

screen Inbox {
  use mailbox: Mailbox
  …
}
```

`state` at the top of a file is the app's: one for the whole app.

A store takes type parameters as a struct does, inferred from its
arguments or from what's expected:

```asuka
store Kept<T>(key: string, initial: T) {
  pub state value: T = initial
}

let size = Kept("size", 17.0)                 // a Kept<float>
let tags: Kept<[string]> = Kept("tags", [])
```

In a store's fns, `self` is the store itself, to hand to what it makes.

### Effects and tasks

```asuka
effect { storage.set("draft", draft) }        // now, and when draft changes
task { try? await mailbox.refresh() }         // once, while the component is shown
task(query) { results = await search(query) } // and again when query changes
```

An effect runs at once and again whenever what it read changes. A task
is async (see below): it starts when the component appears, restarts
when what's in its parentheses changes (cancelling the last run), and
is cancelled when the component goes. The `app` block takes them too:
the app's own, for as long as it runs (a refresh loop, a setting that
follows the app's state: `effect { setColorScheme(scheme) }`).

## Async

Work that waits (the network, a file, a timer) is `async`, and waiting
for it is `await`:

```asuka
async fn loadThread(id: string) throws -> Thread {
  let (messages, people) = try await all(api.messages(id), api.people(id))
  let html = await spawn { renderMarkdown(messages) }
  return Thread(messages, people, html)
}
```

- Calling an async function needs `await`. Handlers, tasks and tests
  may await; a component's body and its `let`s may not.
- **The main thread.** Everything runs on it, and `await` lets other work
  run while yours waits; state can change there and only there.
- `all(a, b)` runs calls at once and waits for every result; `race(a, b)`
  takes the first and cancels the rest; `sleep(300ms)`;
  `withTimeout(5s) { … }`, whose value is its block's (`let page = try
  await withTimeout(5s) { return try await fetch(url) }`), and which
  throws when the time runs out. They're structured: what they cancel has
  stopped, its `defer`s run, before they return or throw.
  `await never()` waits for nothing but the task's end: a task that
  holds something while its component is shown (`defer { release() };
  await never()`), or a stream that ends only when its reader goes.
- `spawn { … }` runs a block on another thread, for work that would hold
  the main thread up (parsing, image processing). It may use values only,
  not state, stores, views, native objects or C pointers, nor a file's
  `let` that holds one (a database): the compiler says so if it does. On
  the web it runs on one of the page's Web Workers, which make the
  file's values for themselves, and nothing else.
- `for await message in socket.messages { … }` reads a stream, a
  `Stream<T>`: values that come over time. `Stream(send => { … })`
  makes one: the block runs as a task when the stream is first read,
  and each `send(x)` is a value; the stream ends when the block
  returns, and fails with what it throws. Leaving the loop (`break`, an
  error, the owner going) cancels the block. A stream has one reader.

  ```asuka
  fn levels() -> Stream<float> = Stream(send => {
    try await battery.watch(changed: level => send(level))
  })
  ```
- `live(initial, source)` is a value a stream keeps current, as a
  function that reads it: read in a binding, it follows `source()`'s
  stream from the first such read until the last reader goes (then the
  stream is cancelled: nothing watches the platform for no one); read
  elsewhere, it's the last value known. It's how a package offers what
  the platform changes, such as the network's state:

  ```asuka
  let level = live(1.0, levels)
  pub fn batteryLevel() -> float = level()
  ```
- **Cancellation.** A task belongs to what started it. When that goes (the
  user leaves the screen), the task is cancelled: the `await` it's at
  ends it, its `defer`s run, and no error is reported.

### Data that loads

`load` keeps an async result in a component:

```asuka
screen Profile(id: string) {
  let user = load api.user(id)   // reloads when id changes

  match user {
    .pending => Spinner()
    .failed(e) => ErrorView(e) { on retry { user.reload() } }
    .ready(u) => ProfileCard(u)
  }
}
```

A `Loading<T>` is `.pending`, `.ready(value)` or `.failed(error)`;
`.value` is the latest value (kept while it reloads, so the screen
doesn't flash), `.reload()` runs it again. When what its arguments read
changes, it runs again and the run that's going stale is cancelled.

## Errors

A function that can fail says `throws`, and calling it needs `try`:

```asuka
enum MailError: Error { offline, server(code: int) }

async fn send(m: Message) throws {
  if !network.online { throw MailError.offline }
  …
}

on press {
  do {
    try await send(draft)
    dismiss()
  } catch MailError.offline {
    showOffline = true
  } catch e {
    alert("Couldn't send", message: e.message)
  }
}

let n = try? parse(text)   // none if it throws
```

`catch` after an expression gives a fallback: the expression's value,
or when what's in it throws, the value of the catch's block (its last
expression, as an `if`'s), unless the block leaves:

```asuka
let port = try parsePort(text) catch e {
  log(e.message)
  80
}
let size = try await fileSize(path) catch { return }
try save(draft) catch { failed = true }   // a statement: no value to give
```

Its clauses are `do`'s: `catch`, `catch e`, `catch e: ParseError`,
`catch MailError.server(code)`, several in a row; the first that takes
the error runs. An error none takes goes on, so the function must throw,
as for `try`. One whose clauses take every error can't throw, so it goes
where a `try` can't: in a component's `let` (`let port = try
parsePort(text) catch { 80 }`, kept up to date as `text` changes), or a
closure that mustn't throw. It works with `try await`, and with `throw`
as a value: `let user = cache[id] ?? throw MailError.missing(id)`.

An error's `message` is its `message` field, its `fn message() ->
string`, or its type's and case's name. `Error("…")` makes an error of a
message. A `catch` never sees a task's cancellation: it goes on to the
task's end, running the `defer`s on its way.

An `Error` is matched by an error type's cases too, as a `catch` takes
them: `match e { MailError.offline => …; MailError.status(code) => …; _
=> e.message }` (an `Error` can be any error, so the match ends with
`_`).

An error a handler or a task doesn't catch goes to the nearest
`Boundary` around the component, and to the app's (shown in
development, logged in release) when there is none:

```asuka
Boundary {
  Feed()
  on error(e) { Text("Something went wrong: {e.message}") }
}
```

## Styles

Every element takes the style props: layout first (`direction`,
`justify`, `align`, `gap`, `padding`, `margin`, `width`, `height`,
`grow`, `shrink`, `absolute`, `top`, `grid`, ...), then look
(`background`, `radius`, `border`, `opacity`, `shadow`, `font`,
`color`, `lines`, ...). See [Layout and style](layout.md).

```asuka
style card {
  padding: 16; radius: 12; gap: 8
  background: .secondaryBackground
}

View { style card; background: .accent }   // later props win
```

A `pub style` is another package's too: `style theme.card`, its values
read in the package that declares it.

An element that sets no style looks like the platform's own.

A container (`View`, `Row`, `Column`, `SafeArea`) takes **variants**:
blocks of style props named after an interaction state, laid over its
style while it's in that state (CSS's `:hover` and `:active`). They are
`hovered`, `pressed`, `dragging` (while it's dragged) and `dragOver`
(while a drag it takes is over it), laid in that order, so `pressed`
wins over `hovered`. A variant sets only the props it names, and its
values are bindings like any prop's:

```asuka
View {
  padding: 16; radius: 12; background: .secondaryBackground
  hovered { borderColor: .accent }
  pressed { background: .separator; opacity: 0.8 }
  on press { open() }
}
```

A view reports a state only once something asks for it, so one with no
`pressed` variant keeps the platform's own pressed look. A variant may
take a `style` too (`pressed { style raised }`). Other views follow a
view's states through its name ([Naming a view](#naming-a-view)):
`View as card { … }` then `card.hovered` (or `card.pressed`,
`card.dragging`, `card.dragOver`) in any binding, a child's (a group's
hover) or a sibling's. What isn't a style uses the handlers: `on
pressedChange(p)`, `on hoverChange(h)`.

## Naming a view

Some things are done to a view rather than set on it: play a video,
run script in a page, put text in a field, scroll a list. A `Ref` is a
view's handle: `ref: r` on any element puts its view in `r`, and
`Element as name` declares one for the rest of the block it's in.

```asuka
component Player {
  let video = Ref()

  Video("intro.mp4") { ref: video }
  Button("Start over") {
    on press {
      video.seek(0)
      video.play()
    }
  }
}
```

A Ref holds its view while the view is shown, and nothing before or
after: a command then does nothing, and a state is false. Its states
are tracked, so a binding that reads one follows it:

| On | Ref's members |
|---|---|
| any view | `hovered`, `pressed`, `dragging`, `dragOver` (bools, tracked); `focus()`, `blur()` |
| `Scroll`, `List` | `scrollTo(x, y, animated: true)` |
| `Video` | `play()`, `pause()`, `seek(seconds)` |
| `WebView` | `try await eval(script)` (its value, as JSON), `run(script)` (without waiting), `goBack()`, `goForward()`, `reload()`, `stopLoading()` |
| `TextField` | `insert(text)`, `format(.bold, on)`, `toggleFormat(.italic)`, `setLink(url)` (a rich field's selection) |

A Ref can be passed along (`shareFiles(paths, from: button)`,
`a11yFocus(field)`), and kept in a file's `let` for views of several
components.

## Animation

`animate { … }` runs its block at once, and the changes it makes to
frames and to what's drawn (opacity, background, a corner's radius, a
shape's trim) animate natively. `animate(.spring) { … }` takes an
`Animation`: its duration, curve, delay, repetitions, a spring's
damping and velocity. Awaited, it returns once the animation is
complete; it needn't be.

```asuka
on press {
  animate { expanded = !expanded }
}
on press {
  await animate(Animation(duration: 500ms, curve: .spring)) { offset = 0 }
  done = true
}
```

`animate(() => …)` and `animate(anim, () => …)` are the same as
closures. `repeat: Animation.forever` repeats until the props change
again. `animating()` says whether the changes made now animate.
`onFrame(t => …)` calls its closure at each frame the display draws,
with the frame's time, until the closure it returns is called or the
component goes: `let stop = onFrame(t => tick(t))`. `frames()` is the
same as a `Stream<Duration>`, drawn only while it's read: `for await t
in frames() { … }` in a task, which stops as the loop ends or the task
goes.

## Drawing

`Canvas(g => …)` draws with a `Graphics`, in points from its top left,
once it's laid out, and again when its size or what it read changes;
`Shape(fn(width, height) -> Path)` is an outline the platform fills and
strokes (`fill`, `stroke`, `lineWidth`, `trimEnd`, `dash`, ...).

```asuka
Canvas(g => {
  let ring = g.path().arc(g.width / 2, g.height / 2, 40, 0, 6.28318)
  g.stroke(ring, .accent, StrokeStyle(width: 8, cap: .round))
  g.text("{percent}%", g.width / 2, g.height / 2, CanvasText(size: 20, align: .center))
}) { height: 100 }
```

A `Path` is built by its methods, which return it: `Path().move(x,
y)`, `line`, `quad`, `cubic`, `rect`, `roundedRect`, `ellipse`, `arc(cx,
cy, radius, start, end, clockwise: true)` (radians from the positive x
axis), `close()`; `g.path()` is an empty one of the canvas's own,
reused from draw to draw. A Graphics is valid while its draw runs:
`width`, `height`, `fill(path, color)` (or a `LinearGradient`, a
`RadialGradient`), `stroke(path, color or gradient, style)`, `save()`,
`restore()`, `translate`, `scale`, `rotate(degrees)`, `opacity`,
`clip(path)`, `transform(a, b, c, d, tx, ty)`, `evenOdd(on)`,
`text(s, x, y, style)`.

`Shader(shader("""…"""))` is a box a GPU program draws: the program is
Kage, compiled with the app (a mistake in it is the compiler's error),
its uniforms set by name (`uniforms: [Uniform(name: "Level", value:
[0.5]), colorUniform("Tint", .accent)]`).

## The system

| Function | What it does |
|---|---|
| `share(text)` | the share sheet, for a text or a link |
| `try await shareFiles(paths, title:, from: ref)` | the share sheet for files, from a view (where a popover points); returns once it's closed |
| `haptic(.success)` | the platform's haptic feedback (a `Haptic`) |
| `copy(text)` | puts text on the clipboard |
| `openURL(url)` | opens url in the app that takes it |
| `announce(text)` | has a screen reader say text |
| `a11yFocus(ref)` | moves a screen reader to the view |
| `try await webCookies(url)` | the web views' cookies for url |
| `useKeys([Shortcut(…)])` | the window's keys while the component lives |
| `onOpenURL(u => …)`, `onOpenFiles(paths => …)`, `onShare(items => …)` | what the app is opened with, or what other apps share with it, while the component lives |
| `quit()`, `relaunch()` | quits the app as the user's Quit does (and starts it again), asking it first; desktop only |
| `closeWindow()` | closes the window the code is in, without asking |
| `onCloseRequest(fn() -> bool)`, `onQuit(e => …)`, `onSecondInstance((args, dir) => …)` | asked before the window the component is in closes; hears the app about to quit (`e.cancel()`), or started again, while the component lives |
| `onBack { … }` | asked when the user goes back from the screen, sheet or popover the component is in: `true` takes it (the screen stays), `false` lets it go |
| `onLifecycle(l => …)` | hears the app go to the foreground and the background, and memory warnings (a `Lifecycle`), while the component lives |
| `alert(title, message:, actions:)` | shows an alert now; its `actions` are `.button`s (none: an OK). An `Alert` element shows one while its `open` is true |
| `snackbar(message, action:, duration:, onDismiss:)` | a brief message at the bottom of the window, with an action (Undo); returns a closure that dismisses it |
| `setColorScheme(.dark)` | the app's appearance: `.light`, `.dark`, or `.system` (the platform's) |
| `environment()`, `screenEnvironment()` | the size, safe area, text size, platform and the rest (tracked): the screen's or sheet's own, and the whole screen's |
| `mainWindow()` | the main window's `Ref`: `show()`, `hide()`, `focus()`... from anywhere |

`useKeys` and the `on…` registrations are items of a component's body
(or the app's block). The app's block also handles them as events: `on
openURL(url) { … }`, `on files(paths) { … }`, `on share(items) { … }`,
`on quit(e) { … }`, `on secondInstance(args, dir) { … }`, `on
foreground { … }`, `on background { … }`. Its first `Window` is the
main window ([Navigation](navigation.md#windows)).

`asuka/assets` reads a file of the package's `assets/` directory as the
app compiles, as Go's `go:embed` does: `assets.text("welcome.md")` is
its text, `assets.bytes("logo.png")` its bytes. A file that isn't there
is a compile error.

## Navigation

```asuka
app {
  Tabs {
    Tab("Inbox", icon: "tray") { Stack { Inbox() } }
    Tab("Settings", icon: "gear") { Stack { Settings() } }
  }
}

screen Inbox {
  title: "Inbox"
  List {
    for m in messages key m.id {
      MessageRow(m) { on press { push(Thread(m.id)) } }
    }
  }
  toolbar {
    Button(icon: "square.and.pencil") { on press { present(Compose()) } }
  }
}
```

A `screen` is a component whose root is a screen of a stack: its block
takes the screen's props (`title`, `largeTitle`, ...) and slots
(`toolbar`, `bottomToolbar`, `primary`). `push(…)` and `pop()` act on
the nearest stack, `present(…)` and `dismiss()` on sheets. `Split` is a
sidebar of `Destination`s (in `Sidebar` sections) beside the chosen
one's stack.

With a `Router`, every screen has an address. A screen declared with
its pattern is routable, its parameters typed; a call of it is its
address, checked as the app compiles:

```asuka
app {
  Router(initial: "/inbox") {
    Route("/inbox", layout: .stack, title: "Inbox") {
      Inbox
      Message
    }
  }
}

screen Inbox at "" {
  title: "Inbox"
  Button("The first") { href: Message(id: 1) } // /inbox/1
}

screen Message(id: int) at ":id" {
  title: "Message {id}"
  Button("Back to the inbox") { on press { navigate("/inbox") } }
}
```

See [Navigation](navigation.md).

## Text in other languages

`tr.inboxTitle(unread: n)` says a message of the app's catalogs
(`locales/en.json`, `de.json`, ...), checked when the app compiles: a
missing key or argument, or one of the wrong type, is an error. A message
said in a binding follows the app's language, so switching it
(`i18n.setLocale("de")`, asuka/i18n) changes what's shown, and a language
written right to left lays the app out from the right. See
[Languages](i18n.md).

## Native code

What only the platform can do goes in a native package, declared in
Asuka and implemented in each platform's own language:

```asuka
pub native interface Haptics {
  fn impact(style: Feedback)
}

pub native component MapView(region: Region) {
  prop pins: [Pin] = []
  event regionChanged(region: Region)
}
```

Making a module and calling it may fail (its platform may have no code
for it), so both take `try`: `let h = try Haptics()`, `try
h.impact(.light)`. `asuka native generate` writes the Swift, Kotlin,
JavaScript and Go each platform implements. See [Native
packages](native.md).

C is called directly. `extern` declarations say what a C function
takes and gives back, in C's types, and Asuka sees each as one of its
own (an `Int32` is an `int`, a `CString` a `string`):

```asuka
extern type Db with release db_close
extern fn db_open(path: CString) -> Db?
extern fn db_close(db: Db)
extern fn db_each(db: Db, sql: CString, row: fn(CString?) -> bool) -> Int32

extern struct Point {
  x: Float32
  y: Float32
}
```

An `extern type` is a pointer C gives, freed by its release as the last
reference goes; an `extern struct` crosses by value; a function passed
to C is a callback. `asuka.toml`'s `[c]` says what C the app compiles
and links. See [C from Asuka](c.md).

## Tests

```asuka
test "archiving removes the row" {
  let app = mount(Inbox(), provide: Mailbox.fake([sample]))
  await app.idle()
  app.tap("Archive")
  app.expectNo("Hello")
}
```

`expect(cond)` fails a test when cond is false, saying what failed and
where (with a comparison's two sides). JSON comes from `asuka/json`:
`let notes: [Note] = try json.decode(text)` reads what's expected of it
(or `json.decode<[Note]>(text)`), `json.encode(notes)` writes it. A
generic function may read or write its `T` as JSON; each call then
needs a `T` JSON can say, which the compiler checks where it's called.

`asuka test` runs a package's tests on a fake host: it lays views out as
the platforms do, measures text at a fixed width per character, and time
moves only when nothing else can run (a `sleep(5s)` in a test takes no
time). `mount` returns the app, to drive and check: `tap`, `longPress`,
`type(_, into:)`, `submit`, `toggle`, `slide`, `select`, `swipe(_,
action:)`, `refresh`, `scrollTo`, `back`, `dismiss`, `pickDate`,
`pickColor`, `expect`, `expectNo`, `texts()`, `disabled(text)`,
`until`, `waitFor`, `idle`; `in(.navigationBar)`, `in(.sidebar)` and
`nextTo(label)` narrow what a text names; with a router, `mount(…, at:
"/inbox/1")`, `href`, `history()`, `goBack()`, `goForward()`. See
[Testing](testing.md).

## Memory and performance

There's no garbage collector, so there are no pauses. Values are
reference counted and copied on write. State, stores, effects and tasks
belong to the component that made them, and go with it.

- A change runs only the bindings that read what changed, and sends the
  platform only the props that changed. The props go in one batch, then
  layout runs, then the frames go in one more batch.
- Release builds are compiled to C with the core, so there's no
  interpreter in an app you ship.
- Lists build only what's on screen and reuse cells.

## Keywords

`app as async await bind break catch component continue defer do effect
else emit enum event extern false fn for if import in interface key let load
match native none on prop provide pub return screen self spawn state
store struct style task test throw throws true try type use var while`
