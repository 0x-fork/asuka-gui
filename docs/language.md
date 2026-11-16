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
| `int` | 64-bit integers: `42`, `1_000`, `0xff` |
| `float` | 64-bit floating point: `3.14`, `1e-3` |
| `bool` | `true`, `false` |
| `string` | UTF-8 text: `"Hello, {name}"` |
| `bytes` | raw bytes |
| `[T]` | arrays: `[1, 2, 3]`, `[]` |
| `[K: V]` | maps, kept in insertion order: `["a": 1]`, `[:]` |
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

A map's keys are ints, strings, bools or enums without payloads.

### Built-in methods

Strings, bytes, arrays, maps and optionals have methods; those that change
their value (`push`, `insert`, `removeAt`, `removeWhere`, `pop`,
`clear`, `sort`, `remove`) take a `var` or state:

| Type | Methods |
|---|---|
| `[T]` | `count()`, `count(where)`, `isEmpty()`, `first()`, `last()`, `get(i)` (a `T?`), `push(x)`, `insert(x, at: i)`, `removeAt(i)`, `removeWhere(p)`, `pop()`, `clear()`, `map(f)`, `filter(p)`, `any(p)`, `all(p)`, `index(p)` (an `int?`), `find(p)`, `contains(x)`, `sum()`, `reversed()`, `slice(from, to)` (`to` left out: the end), `prefix(n)`, `suffix(n)`, `sorted()`, `sorted(by)`, `sort()`, `sort(by)`, `enumerated()`, `joined(separator:)` (of strings), `+` |
| `[K: V]` | `m[k]` (a `V?`), `m[k] = v`, `count()`, `isEmpty()`, `has(k)`, `get(k)`, `remove(k)`, `keys()`, `values()` (in insertion order) |
| `string` | `count()` (characters), `byteCount()`, `isEmpty()`, `upper()`, `lower()`, `trim()`, `trimStart()`, `trimEnd()`, `trimPrefix(s)`, `trimSuffix(s)`, `contains(s)`, `hasPrefix(s)`, `hasSuffix(s)`, `replace(old, new, count:)` (every one unless count says), `split(sep, count:)`, `fields()` (the words), `lines()`, `find(s)`, `findLast(s)`, `findAny(chars)` (byte offsets, or none), `slice(from, to)` (characters), `byteSlice(from, to)` (bytes: the offsets find gives), `padStart(width, with:)`, `padEnd(width, with:)`, `toInt()`, `toFloat()`, `repeat(n)`, `chars()`, `bytes()`, `+` |
| `bytes` | `count()`, `isEmpty()`, `get(i)` (an `int?`), `slice(from, to)`, `concat(b)`, `string()` (UTF-8; what isn't becomes U+FFFD), `hex()` and `hex(separator)` (`"04 a1"`); `bytes()` is none, `bytes([0x04, 0xa1])` those, `s.bytes()` a string's UTF-8 |
| `int` | `float()`, `abs()`, `padded(width)` (`7.padded(2)` is `"07"`) |
| `float` | `fixed(digits)`, `rounded()`, `floor()`, `ceil()`, `int()` (toward zero), `abs()` |
| `Color` | `opacity(a)`: drawn at a times its opacity (a semantic color stays semantic); `hex()` (`"#0a84ff"`; a semantic color, its name: `".accent"`) |
| `Duration` | `nanoseconds()`, `milliseconds()` (ints), `seconds()`, `minutes()`, `hours()` (floats), `rounded(to:)`, `truncated(to:)`, `+`, `-`, `* n` (an int or a float: `1.5 * 1h`), `/ n`, `/ d` (another Duration: how many times it goes in, a float) |
| `Length` | `* n` (`width: 50% * 2`, a point length scaled) |
| `T?` | `isNone()`, `isSome()` |

`min`, `max`, `abs`, `float(n)` and `int(x)` are functions;
`asuka/math` has the rest (`math.sqrt`, `math.pow`, `math.pi`, ...).

### Value semantics

Strings, arrays, maps, structs and enums are **values**: assigning one
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
expected.

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
every case (or end with `_ =>`); patterns bind payloads (`.from(p)`),
match literals (`0`, `"x"`), several cases (`.a | .b`) and take guards
(`.from(p) if p.vip =>`). A case's labeled payload with a zero value
(`false`, `0`, `""`, an empty array or map, `none`, an enum's first case)
may be left out where it's made: `.button(title: "Copy", run: copy)`
leaves `Action.button`'s `icon`, `role`, `disabled` and `checked` so.

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
b) => expr` or `x => { statements }`; it captures values by value and
state by reference.

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

Generic functions and types take type parameters; `Equatable`,
`Comparable` and `Hashable` are the constraints, and structs and enums
have them when their fields do.

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

Statements: `let`, `var`, assignments (`= += -= *= /= %= ??=`), `if`,
`for x in xs`, `for (i, x) in xs.enumerated()`, `while`, `match`,
`return`, `break`, `continue`, `throw`, `do { } catch { }`, `defer { }`,
`emit`.

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
  `withTimeout(5s) { … }`. They're structured: what they cancel has
  stopped, its `defer`s run, before they return or throw.
  `await never()` waits for nothing but the task's end: a task that
  holds something while its component is shown (`defer { release() };
  await never()`), or a stream that ends only when its reader goes.
- `spawn { … }` runs a block on another thread, for work that would hold
  the main thread up (parsing, image processing). It may use values only,
  not state, stores or views: the compiler says so if it does. On the
  web it runs on one of the page's Web Workers.
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

An error's `message` is its `message` field, its `fn message() ->
string`, or its type's and case's name. `Error("…")` makes an error of a
message. A `catch` never sees a task's cancellation: it goes on to the
task's end, running the `defer`s on its way.

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
else emit enum event false fn for if import in interface key let load
match native none on prop provide pub return screen self spawn state
store struct style task test throw throws true try type use var while`
