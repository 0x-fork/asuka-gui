# Lowering Asuka

How the compiler turns Asuka (docs/language.md, RFC 0007's grammar) into
bytecode (lang/bytecode) that the VM (core/src/vm) runs. Release builds
compile that bytecode to C (lang/cgen): each synchronous function
becomes a C function that calls the same instruction bodies the
interpreter does (core/src/vm/ops.h), and async functions stay on the
VM. So the bytecode is the one contract, and both backends mean the
same thing by it (core/vm_test.go runs every module both ways).

## Pipeline

```
lang/syntax   source -> AST (parser, printer)
lang/check    AST -> typed program: scopes, types, the reactive analysis
lang/ir       typed program -> IR: functions of three-address code over
              typed virtual registers; reactive constructs as runtime
              calls; async marked; retain/release implicit in kinds
lang/bytecode IR -> module (register allocation per kind, jump patching,
              handler tables, line tables)
lang/cgen     bytecode -> C (release)
              typed registers (the VM's opcodes, registers allocated per
              kind as it lowers, jumps to labels); reactive constructs
              as runtime calls; async marked; retain/release implicit in
              kinds. Encode writes the module: labels to offsets,
              handler tables, line tables (it lives in lang/ir because
              the IR's instructions are lang/bytecode's opcodes)
lang/cgen     IR -> C (release)
lang/compile  the driver: a package and its imports -> one module
```

A program is compiled whole: the app's packages, their dependencies and
the prelude (lang/prelude: types.asuka, enums.asuka, elements.asuka, and
the built-in functions and methods) into one module.

## Types to kinds

| Asuka type | Kind | Representation |
|---|---|---|
| `int`, `Duration` (ns), `Color` (ops.Color), `Length` | int | as is; a Length packs its unit (0 auto, 1 points, 2 percent) in bits 32-39 and its float32 value in bits 0-31 |
| payloadless enums | int | the case's index (a wire enum's value) |
| `bool` | bool | 0 or 1 |
| `float` | float | |
| `string` | ref | ak_str |
| `bytes` | ref | ak_bytes |
| `[T]` | ref | ak_arr, element kind T's, shape T's |
| `[K: V]` | ref | ak_map |
| struct, tuple | ref | ak_struct with its shape |
| enum with payloads, or declared `: Error` or having an interface | ref | ak_enum (payloadless cases: shared immortals) |
| `T?`, T a reference | ref | NULL for none |
| `T?`, T a scalar | ref | NULL, or an ak_box |
| `fn(...)` | ref | ak_closure |
| store | ref | an ak_struct of the store's cells, memos and methods' captures |
| `View` | ref | ak_view (the UI runtime's) |
| `Task<T>` | ref | ak_future |
| `Loading<T>` | ref | the runtime's loader (see "load") |
| interface | ref | a struct or enum value; calls go through CALLI |
| `void` | void | |

Generic code is compiled once per instantiation (monomorphized). A
generic store too: its function, its env's shape and its fns are made
per instantiation (`Box<int>`, `Box<[string]>`), and a let of its type
parameters is a memo, so its env holds references whatever T is.

Shapes are named so that JSON and hot reload find them: a type of the
root package or the prelude by its name (`Todo`), of another package by
its path (`./model.Todo`), an instantiation with its arguments
(`Box<int>`), a tuple by its elements (`(int,string)`), a component's or
a store's env by the component's name.

A map's keys are what the runtime's maps hash: ints (and what's stored
as one: bools, payloadless enums, durations, colors) and strings.

## Registers

Each register has one kind for the whole function. The lowering
allocates them as it goes: a temp lives until the end of the statement
that made it, a local until the end of its block (a reference local is
cleared there, so a value someone changes next is unique, and changed in
place), and a run of consecutive registers of given kinds (a call's
arguments, a struct's fields, a closure's captures) is reused only for
the same kinds. An argument block's references are cleared after the
call for the same reason.

An expression's value may be a local's own register, borrowed: whoever
changes a value copies it first, or changes it in place through its
owner (see "Changing values in place").

## Changing values in place

`x.f = v`, `xs[i] = v`, `m[k] = v` and the builtins that change their
receiver (`push`, `insert`, `removeAt`, `removeWhere`, `pop`, `clear`,
`sort`, `remove`) first compute every index and argument, then reach the
value through its owner:

- a local: SETF/SETIDX/MSET on its register; an inout builtin gets it
  moved (MOV, no reference counted) into the first register of its
  argument block and moved back after, so a value only the local holds
  is changed in place;
- a boxed var: VGET, change, VSET;
- state (a cell: a component's or a store's, or a file's): CTAKE,
  change, CPUT, with a handler that puts it back if the change throws;
- a field or an element of those: read it (GETF, IDX), change it, write
  it back (SETF, SETIDX).

A builtin that may raise an error (`removeAt` out of range, one that
calls a closure) runs in a handler that moves the receiver back first.

## Functions

- A function is a FuncDef; its parameters are its first registers.
  Methods take `self` first. Closures capture by value; a `var` a
  closure captures and changes is a box (VBOX, VGET, VSET).
- Default arguments are filled in at the call site (a default may use
  only package-level names; a generic function's is lowered with the
  call's type arguments).
- A closure's function takes exactly the parameters its caller passes:
  an event handler the event's values (those it doesn't name are
  unused registers), a row builder (item, index), a key function the
  item, a thunk or a builder none. It captures what its body uses of
  the function that makes it, in its capture list's order, and loads
  them (CAP) as it starts.
- A struct's or an enum's methods take `self` first; inside them, the
  fields and methods are in scope by their bare names. A struct's
  methods can't change it (it's a value): they return a new one.
- `throws` sets FThrows; `async` sets FAsync. Calling an async function
  is a plain CALL: it runs on the caller's coroutine (a task) and
  suspends it if it waits. AWAIT waits on a future: a `Task<T>`, what an
  async builtin returns (`time.sleep`, `http.get`), `all`, `race`,
  `spawn`.
- `spawn { body }` is CALLB `async.spawn(closure, resultKind)`. The
  checker allows captures of values only (no cells, stores, views,
  closures holding them, tasks), and no reads of module state in what
  the body calls (directly, or through what those call). Its value is
  its `return`'s, or its block's last expression's.
- `all(f(a), g(b))` evaluates the calls' arguments, starts each call as
  a task (TASK of a closure capturing them), and awaits CALLB
  `async.all` of the array of their futures. Its tasks return
  references (a scalar boxed), so the one array of results holds them
  all; the tuple is made of its elements (UNBOX for scalars).
  `race(…)`'s calls have one type, and its tasks return it as it is.
  `all(tasks)` and `race(tasks)` over a `[Task<T>]` pass it through.
- `sleep(d)`, `all`, `race` and `idle` are futures awaited at once:
  AWAIT follows the CALLB. `await` of a call of an async function is
  that CALL; of a `Task<T>`, AWAIT. `await` of something that doesn't
  wait does nothing.

## Errors

- `throw e` is THROW (of a copy: THROW moves its register).
- An error type (`struct E: Error`, `enum E: Error`) is stored as an
  enum whose case 0 no value has: an error enum's cases are 1, 2, ...,
  and an error struct is the one case 1 whose payload is its fields
  (read with GETP; its fields can't be assigned). So a caught error's
  ECASE tells the three kinds apart: a user error is not 0; a runtime
  error (`ak_shape_error`: division by zero, an index, JSON, `Error("…")`)
  and the cancellation are structs, 0. Of those, a runtime error equals
  (EQ) `error(error.message(e))`, the error made of its own message; the
  cancellation doesn't.
- Every error shape has two methods CALLI calls: `__etype`, an id of its
  type, and `message` (its `message` field, its `fn message() ->
  string`, or its type's and case's name). `e.message` on an `Error` is
  CALLI `message` for a user error, `error.message` for a runtime one.
- A `do { } catch` is a handler range over the `do` block. Its code,
  placed after the range (so a THROW from it goes to the handlers
  around, not to its own), CATCHes the error, rethrows the cancellation
  at once (no catch sees it: it unwinds to the task's end, through every
  `defer`), then tests the clauses in order: `catch`, `catch e`, `catch
  e: Error` take any error; `catch e: T` compares `__etype`; `catch
  T.case(p)` that and the case and its payload's patterns. None
  matching THROWs it on.
- `try?` is a handler that turns any error but the cancellation into
  none.
- `defer { }` protects the rest of its block: its code is emitted at the
  block's end, before each return, break or continue that leaves it,
  and in a handler that runs it and THROWs the error on.
- Handlers are listed innermost first.
- A test's body is in a handler that rethrows a user error as a runtime
  error of its message, so the runner (which reads runtime errors'
  messages) reports it.

## Strings and values as text

`"a {x} b"` is INTERP over a block of its parts: the text between
interpolations (KSTR), and each value: ints, floats and bools as they
are (INTERP prints them), strings as they are, and the rest as text: a
payloadless enum by its case's name (an array of the names, frozen in a
global), a `Duration` in milliseconds (`300ms`), an optional as `none`
or its value, an error as its message, anything else as its JSON
(`json.encode` with its descriptor). `print(x)` prints the same text.

A multi-line string's text drops its first line when that's blank, and
the indentation of its closing quotes' line from every line.

## Module state and constants

- `state` at the top of a file is a module global holding a cell, made
  in the module's init function (CELLK keyed `package.name`).
- A top-level `let` that reads state is a memo global, made in init; one
  that doesn't is a constant.
- Constants of reference kind (string literals aside, which are already
  in the string table): a struct literal with constant fields, an array
  of constants, a struct's `let` (Font.title), are built once in init
  into a global and frozen (`rt.freeze`, which makes them and what they
  hold immortal, so spawn's threads may share them). Today the lowering
  does it for top-level lets and types' lets; literals in functions are
  built where they are (hoisting them is an optimization still to do).
- Init makes each package's globals, the packages in dependency order
  and each package's globals by what their initializers read (a cycle
  is the checker's error). It first calls `init.strings`, which makes
  the frozen tables of names the lowering asked for (enums' case names).

## Reactivity

Reads, by what's read:

| Read of | In a reactive context (a binding, a memo, an effect) | In a handler or function |
|---|---|---|
| a cell (`state`) | CGET: tracked | CGET (the runtime runs handlers untracked) |
| a memo (`let` in a component, a store, a file) | MREAD | MREAD |
| a parameter or prop of a component | AGET of its accessor | AGET |
| a `for` row's item, a `match` arm's payload | AGET | AGET |

Writes: `x = e` is CSET. A write into part of a cell's value
(`todos[i].done = true`, `form.name = n`, `todos.push(t)`) takes the
value out (CTAKE), changes it in place (SETF, SETIDX, an inout builtin
on the register), and puts it back (CPUT, which always notifies). The
index and the new value are computed before the CTAKE.

A **binding** is any expression the UI follows: a prop's value, an
`if`'s condition, a `for`'s items, a `match`'s subject, a `let`. The
compiler makes it a closure of no arguments (a thunk) and passes it to
the runtime, which runs it as a render effect, or as a memo for `let`.
An expression that reads nothing reactive (a literal, a constant, a
local of the enclosing function, arithmetic on those) is evaluated once
and passed as a value instead (`ak_ui_set` rather than `ak_ui_bind`).

## Components

A component compiles to a function returning a View. Its arguments
(all references) are its header parameters, then its props, then its
events' handlers, in declaration order:

- A parameter or prop is passed as an **accessor**: ACCV of the value
  when the caller's expression is constant, ACCC of a cell or memo when
  it's one read alone, ACCF of a thunk otherwise. A prop the caller
  leaves out is none, and the callee's default applies (evaluated in the
  callee, as an accessor too).
- A prop of type `View` (a slot, `children`) is passed as a builder: a
  closure that builds the view when called, so it's built in the
  callee's owner, where the callee places it, only when it's shown.
- An event's handler is a closure, or none (emit then does nothing).

Its members live in its **env**, a struct of the component's shape:
a field per parameter, prop and event (their accessors, builders and
handlers), per state (its cell), per let (its memo, its loader, or its
value when it reads nothing reactive) and per use. The closures the
component makes reach its members through the env: those made before
the env is whole (its lets' memos) capture a box (VBOX) the env is put
in (VSET) once it's made, the rest the env itself. Its `fn` members are
functions taking the env first. A store is the same, and its value is
its env.

The function's body, in order:

1. `state x = e`: CELLK with the key `Component.x` (the runtime adds the
   instance's path: its place in the tree, the row's key).
2. `let y = e`: MEMO of a thunk (or a constant when it reads nothing).
3. `use x: T`: CALLB `ui.use(key)`, the key being T's type name.
   `provide e`: CALLB `ui.provide(key, value)`, on the component's owner,
   before the views are built.
4. `fn` members: closures over the component's cells and memos.
5. `effect { }`: EFFECT of a closure. `task { }`: CALLB `ui.task`, which
   starts the closure as a task when the component mounts.
   `task(a, b) { }` is an effect that reads `a` and `b` and starts the
   task in its own owner, so a re-run cancels the run before.
6. The view items: one element is the component's view; several make a
   fragment (CALLB `ui.fragment`, then `ui.add` each).

`screen` is a component whose root is the `Screen` element: the
screen's prop sets and slots go to that element, its view items are the
element's children.

`emit e(args)` calls the handler argument with args when it isn't none
(CALLC; its result register is a reference, so the future of an async
handler started from C is released).

A store's constructor is the same function without the views: it
returns its env. Its `task { }` is TASK (a store has no mount), owned by
the owner that made the store, so disposing that owner cancels it.

## Elements

A built-in element (lang/prelude/elements.go) lowers to the UI runtime's
protocol (core/src/ui/ui.h), as builtins. Their names and arguments are
listed in one place, lang/ir/uimap.go; the numbers (AK_EL_, AK_KEY_,
AK_EV_) are lang/prelude's ElementID, KeyID and EventID, which match
core/src/ui/elements_gen.h:

| Source | Lowered |
|---|---|
| `Button("Add") { … }` | `v = ui.new(AK_EL_BUTTON)`; `ui.set(v, AK_KEY_BUTTON_TITLE, "Add")` |
| `color: c` (constant) | `ui.set(v, AK_KEY_COLOR, c)` |
| `color: active ? .accent : .label` | `ui.bind(v, AK_KEY_COLOR, thunk)` |
| `on press { … }` | `ui.on(v, AK_EV_BUTTON_PRESS, closure)` (async when it awaits) |
| `bind text: q` | `ui.bind(v, AK_KEY_TEXT_FIELD_TEXT, thunk{CGET q})` and `ui.on(v, AK_EV_TEXT_FIELD_CHANGE, closure(t){CSET q t})` |
| `style card` | the style's prop sets, inlined here; a later set of the same key wins (the compiler keeps only the last) |
| children | each built, then `ui.add(v, child)` |
| `toolbar { … }` | `ui.slot(v, AK_KEY_SCREEN_TOOLBAR, fragment)` |
| `Text(x) as t` | `t` is a local holding `ui.ref(v)` |

Values cross as their kinds (rt.h). Conversions the checker inserts:
a number where an `Edges` is expected is `Edges(all)`; `[v, h]` and `[t,
r, b, l]` are Edges; a number where a `Length` is expected is points;
`.label` where a `Color` is expected is the semantic color.

## Control flow in views

| Source | Lowered |
|---|---|
| `if c { A } else { B }` | `ui.show(thunk{c}, builder{A}, builder{B})` |
| `if let u = user { A }` | `ui.show(thunk{user != none}, builder{ let u = memo(user!); A })` |
| `for x in xs key k { R }` | `ui.for(thunk{xs}, closure(x){k}, closure(xAcc, iAcc){R})`; `x` in R is AGET xAcc |
| the same, directly in a `List` | `ui.lazy_for(list, …)` with the same arguments |
| `match s { .a => A; .b(p) => B }` | `ui.switch(thunk{the index of the arm s matches, -1 for none}, closure(arm){ switch on it: A, or { let p = memo(payload of s); B } })` |

Each builder and row runs under its own owner, which the runtime
disposes when the branch or row goes.

The selector of a match returns the arm (not the case), so literal,
tuple and `|` patterns work as in statements; a match in a view takes
no guards. A `for` without `key` passes none, and the runtime keys rows
by their index. Several view items (a component's body, a branch, a
row) make a fragment (`ui.fragment`, `ui.add` each); a `View?` (an
optional slot) is added only when it isn't none.

## load

`let user = load api.user(id)` is CALLB `ui.load(thunk)`, where the thunk
reads the arguments (tracked) and returns the future of the call (the
async function started as a task). The runtime re-runs the thunk when
what it read changes, cancelling the last task, and exposes a
`Loading<T>` whose `.pending`, `.ready(v)`, `.failed(e)`, `.value` and
`.reload()` the prelude declares as builtins (`load.state`, `load.value`,
`load.reload`); its state is a cell, so reading it is tracked.

## Navigation and commands

`push(X(…))`, `present(X(…))`: CALLB `ui.push(builder)`, `ui.present(builder)`.
`pop()`, `dismiss()`, `field.focus()`, `scroll.scrollTo(…)`: builtins
taking the ref.

## Tests

A `test` block is a function in Module.Tests (async when it awaits).
`expect(cond)` and `expect(cond, message)` throw a runtime error when
cond is false, saying what failed and where (`expected count == 2
(app.asuka:12): got 1 and 2`, a comparison's sides shown). `asuka run`
adds `fn main()` as a test named main. `mount(view)` is
CALLB `test.mount(builder)`, which mounts the view on the fake host and
returns an app handle. `app.tap(text)`, `app.type(text, into:)`,
`app.expect(texts...)`, `app.expectNo(text)`, `app.idle()` are builtins
on it (`test.tap`, ...); a failed expectation throws, and the runner
reports the error with its line.

## Hot reload

State cells are made with CELLK: the runtime looks the key up in what
the last run kept. On a reload, the runtime keeps every keyed cell's
value (converted to the new module's types by field and case names; a
value whose type changed in a way that can't be converted starts
again), disposes the old app, frees the old module once nothing of it
is left, and mounts the new one.

## JSON

`json.decode(text)` (asuka/json), whose T is what's expected of it
(`let notes: [Note] = try json.decode(text)`: the grammar has no type
arguments in expressions), `json.encode(value, indent: 0)` and the
network's `.json()` lower to the builtins `json.decode(text, desc)` and
`json.encode(value, desc, indent)`, where desc is T's descriptor
(core/src/rt/json.h) as a string constant. For the runtime to read and
write a struct's fields by name, each field's name in its shape carries
its descriptor after a NUL (`"title\x00s"`, `"tags\x00[s"`), and the same
for enum cases' labelled payloads; a payloadless enum JSON reads or
writes gets a shape of its name whose cases name its values.

## Native packages

A `native interface X { fn m(a: A) -> R }` (docs/native.md) lowers to an
object type (a reference: the runtime's ak_native_obj) and its methods:

- `X()` (making one) is CALLB `native.new(cls, iface, errors)`: cls is
  the class each platform's generated code registers for the package
  (its module ID), iface the interface's index in the package, errors
  the package's declared errors (an array of Error values, constants).
- A call `x.m(a)` is CALLB `native.call(x, method, args, argsDesc,
  retDesc, errors)`: method the method's index from 1, args a tuple of
  the arguments, the descriptors (core/src/native/native.h: rt/json.h's
  plus `o` objects, `F<n>…` callbacks, `v` nothing) string constants. An
  `async` method is `native.start` with the same arguments, then AWAIT;
  cancelling the task cancels the call on the platform's side.
- Every method `throws` (the platform's side may fail); the checker
  requires `try` at calls.

A native interface can't be generic, has methods only, and no struct
declares it. The class is the package's name as internal/nativegen
names it: asuka.toml's `name`, else its directory's. The errors array is
a global init.strings makes: every enum declared `: Error` in the
package, its cases in order (a case with values stands as an error of
its name).

`asuka native generate` writes each platform's side (Swift on iOS and macOS, Kotlin,
JavaScript, Go on Windows and Linux, whose hosts are Go) from the same declarations, in
native/codec.go's wire format.

## The standard library's native fns

In a standard library package (`asuka/<name>`, std/), `native fn f(…) ->
R` has no body: the runtime implements it as the builtin `<name>.f` (a
method's `native fn` in `struct T`: `<name>.T.f`, the receiver first). Its
function's body is CALLB of that builtin with the parameters (AWAIT of
the future it returns when it's `async`), so it's called, passed and
compiled as any function. Other packages reach platform code through
native interfaces instead.

## Streams

`Stream(produce)` is CALLB `stream.new(produce, kind)` (kind: the
values' ak_kind); `for await x in s { … }` is: s into a local; at the
top, AWAIT CALLB `stream.next(s)` (a bool: whether a value came; it
throws what the producer threw), JF to the end, CALLB `stream.value(s)`
for x, the body, back to the top; at the end CALLB `stream.close(s)`,
which cancels the producer (core/src/rt/stream.c).

## Live values

`live(initial, source)` is CALLB `live.new(initial, source, kind)`,
which returns a closure that reads the value (core/src/vm/live.c): a
memo over a signal the stream's values write. The memo's first tracked
read queues the stream's start (on the ready queue: a producer's first
steps may write state, which a memo's update can't); its unwatched
hook cancels the stream. An untracked read returns the last value and
starts nothing.

## Imports

The compiler (lang/compile) finds each import: `./path` beside the
importing file's package; `asuka/json`, `asuka/math` and `asuka/time`
in the checker itself; any other `asuka/<name>` in std/ (the CLI's
copy: package std); anything else through the project's asuka.lock
(internal/pkgs.Imports), by the importing package's dependencies, so
two packages may import different majors under one name. A package's
symbols are qualified by its import path (a dependency's relative
imports by the dependency's path), so two packages' `./util` are two
packages.

## Prelude functions the runtime provides

| Asuka | Lowered |
|---|---|
| `environment()` | CALLB `ui.environment` (an Environment, tracked: read in a binding, it follows the screen) |
| `push(v)`, `present(v)` | CALLB `ui.push(builder)`, `ui.present(builder)` (the argument as a builder) |
| `pop()`, `dismiss()` | CALLB `ui.pop`, `ui.dismiss` |
| `sleep(d)` | CALLB `time.sleep(ns)`, AWAIT |
| `all(a, b, …)`, `race(…)` | the calls as tasks (TASK), CALLB `async.all`/`async.race` over an array of them, AWAIT |
| `spawn { … }` | CALLB `async.spawn(closure, kind)`, a Task |
| `print(s)` | CALLB `print` |
## Built-in methods

The built-in types' methods are a table (lang/check/methods.go's
Methods): a receiver (an array, a map, a string, a float, an optional,
mount's app, a Loading), a name, a signature over the receiver's type
variables, and the builtin (core/src/rt/lib.c) CALLB calls, the
receiver first, then the arguments in the builtin's order. An inout
method changes its receiver in place (see "Changing values in place").
A few the lowering does itself: `count()` of an array or a map is LEN,
`isNone()` ISNONE, a map's `get(k)` MGET, `enumerated()` a loop making
`(index, element)` tuples (and `for (i, x) in xs.enumerated()` makes
none: it's a loop over the indexes). `map` passes its result's element
kind last. Closures passed to them don't throw and don't await.

## What isn't lowered yet

- A `Length` from a number computed at run time (constants are packed
  when compiling), `for await`, `withTimeout`, native interfaces and
  components, screens (the runtime has no `Screen` element yet), styles
  of other packages, `List`'s lazy rows (no `List` element yet; the
  lowering calls `ui.lazy_for` when an element says it builds rows
  lazily).
- The C UI runtime's builtins (lang/ir/uimap.go) don't exist yet, so a
  module that makes views doesn't load ("no builtin ui.new").
- The runtime reports a user error's message only where the lowering
  asks for it (tests, `e.message`): `ak_error_message` reads a struct's
  `message` field, and user errors are enums; it could call the shape's
  `message` method (VM shapes have it) so that boundaries and the
  app's error reports show them too.
