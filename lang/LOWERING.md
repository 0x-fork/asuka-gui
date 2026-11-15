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
| `Graphics`, `Path`, a catalog | ref | the UI runtime's objects (core/src/ui/canvas.c, i18n.c) |
| `Task<T>` | ref | ak_future |
| `Loading<T>` | ref | the runtime's loader (see "load") |
| interface | ref | a struct or enum value; calls go through CALLI |
| `void` | void | |

Generic code is compiled once per instantiation (monomorphized).

## Functions

- A function is a FuncDef; its parameters are its first registers.
  Methods take `self` first. Closures capture by value; a `var` a
  closure captures and changes is a box (VBOX, VGET, VSET).
- Default arguments are filled in at the call site.
- `throws` sets FThrows; `async` sets FAsync. Calling an async function
  is a plain CALL: it runs on the caller's coroutine (a task) and
  suspends it if it waits. AWAIT waits on a future: a `Task<T>`, what an
  async builtin returns (`time.sleep`, `http.get`), `all`, `race`,
  `spawn`.
- `spawn { body }` is CALLB `async.spawn(closure, resultKind)`. The
  checker allows captures of values only (no cells, stores, views,
  closures holding them, tasks), and no reads of module state in what
  the body calls.

## Errors

- `throw e` is THROW. A `do { } catch` is a handler range over the
  `do` block: its target CATCHes the error into a register, tests the
  catch patterns in order (ECASE and comparisons; `catch e: T` checks
  the shape), runs the first that matches, and RETHROWs when none
  does.
- `try?` is a handler that turns the error into none.
- `defer { }` runs on every exit: its code is emitted before each
  return and at the end of the block, and a handler over the block runs
  it and RETHROWs.
- Handlers are listed innermost first.

## Module state and constants

- `state` at the top of a file is a module global holding a cell, made
  in the module's init function (CELLK keyed `package.name`).
- A top-level `let` that reads state is a memo global, made in init; one
  that doesn't is a constant.
- Constants of reference kind (string literals aside, which are already
  in the string table): a struct literal with constant fields, an array
  of constants, a struct's `let` (Font.title), are built once in init
  into a global and frozen (`rt.freeze`, which makes them and what they
  hold immortal, so spawn's threads may share them).

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

A component compiles to a function returning a View. Its arguments are
its header parameters, then its props, then its events' handlers, in
declaration order:

- A parameter or prop is passed as an **accessor**: ACCV of the value
  when the caller's expression is constant, ACCC of a cell or memo when
  it's one read alone, ACCF of a thunk otherwise. A prop the caller
  leaves out is none, and the callee's default applies (evaluated in the
  callee, as an accessor too).
- A prop of type `View` (a slot, `children`) is passed as a builder: a
  closure that builds the view when called, so it's built in the
  callee's owner, where the callee places it, only when it's shown.
- An event's handler is a closure, or none (emit then does nothing).

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

`emit e(args)` calls the handler argument with args when it isn't none.

## Elements

A built-in element (lang/prelude/elements.go) lowers to the UI runtime's
protocol (core/src/ui/ui.h), as builtins:

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
| `match s { .a => A; .b(p) => B }` | `ui.switch(thunk{case index of s}, closure(case){ switch on it: A, or { let p = memo(payload of s); B } })` |

Each builder and row runs under its own owner, which the runtime
disposes when the branch or row goes.

## Interaction states and variants

A container's (`View`, `Row`, `Column`, `SafeArea`: the elements whose
`Variants` is set in lang/prelude/elements.go) block may hold variants:
a block named after an interaction state (prelude.Variants: `hovered`,
`pressed`, `dragging`, `dragOver`), holding style prop sets only. The
parser reads `pressed { … }` as a named slot; the checker knows it's a
variant from the element. Each prop set in it lowers as a prop set does,
with the state:

| Source | Lowered |
|---|---|
| `pressed { opacity: 0.6 }` | `ui.variant(v, AK_STATE_PRESSED, AK_KEY_OPACITY, 0.6)` |
| `hovered { color: c }` (c reactive) | `ui.variant_bind(v, AK_STATE_HOVERED, AK_KEY_COLOR, thunk)` |
| `style card` inside a variant | the style's sets, inlined into the variant |
| `View as card { … }` then `card.hovered` | `card` is `ui.ref(v)`; the read is `ui.state(card, AK_STATE_HOVERED)`, a tracked bool |
| `on pressedChange(p) { … }` | `ui.on(v, AK_EV_VIEW_PRESSED_CHANGE, closure)`, as any event |

The runtime lays the variants of the states the view is in over its own
values, in the states' order (a later one wins), whole values (a `font`
in a variant replaces the font). A view reports a state from the first
variant, read or handler that asks for it, and leaves each state it's in
when its owner is disposed. `ref` names any element, so `card.hovered`
works on any view the host reports states for.

## Graphics

`Canvas(draw)` takes a closure `fn(Graphics)`; `Shape(path)` one of
`fn(float, float) -> Path` (the width and height it's laid out at).
Both run in a render effect once the element knows its size (after the
frames, as a layout handler does), and again when it changes or what
they read changes; only a drawing that differs from the last is sent.

`Graphics` and `Path` are built-in reference types (runtime objects, not
values): a Graphics is valid only while its draw runs; a Path is a
builder whose methods add to it and return it, so they chain. Their
methods are builtins taking the object first:

| Source | Lowered |
|---|---|
| `g.width`, `g.height` | `canvas.width(g)`, `canvas.height(g)` (floats) |
| `g.path()` | `canvas.path(g)`: one of the canvas's paths, emptied, reused from draw to draw (drawing again allocates nothing) |
| `Path()` | `path.new()` |
| `p.move(x, y)`, `.line`, `.quad(cx, cy, x, y)`, `.cubic(...)`, `.rect`, `.roundedRect(x, y, w, h, r)`, `.ellipse`, `.arc(cx, cy, r, start, end, clockwise)`, `.close()` | `path.move(p, x, y)`, ..., `path.rounded_rect`, `path.arc` (radians, clockwise from +x), `path.close(p)`; each returns p |
| `g.fill(p, color)`, `g.stroke(p, color, StrokeStyle(...))` | `canvas.fill(g, p, c)`, `canvas.stroke(g, p, c, style?)` |
| `g.fill(p, LinearGradient(...))`, `RadialGradient` | `canvas.fill_gradient(g, p, gr)`, `canvas.fill_radial(g, p, gr)` (the checker picks by type) |
| `g.stroke(p, style, gradient)` | `canvas.stroke_gradient` / `canvas.stroke_radial` |
| `g.save()`, `.restore()`, `.translate(x, y)`, `.scale(x, y)`, `.rotate(deg)`, `.opacity(a)`, `.clip(p)`, `.transform(a, b, c, d, tx, ty)`, `.evenOdd(on)` | `canvas.save(g)`, ..., `canvas.even_odd(g, on)` |
| `g.text(s, x, y, CanvasText(...))` | `canvas.text(g, s, x, y, style?)` |

`Shader(program)` takes the compiled program as `bytes`: the compiler
compiles `shader("""…""")`, a Kage program, at build time (package
shader, internal/shadercode's Encode, which carries the uniforms' lane
table) into a bytes constant. `uniforms: [Uniform(name:, value:)]` set
its variables by name; `colorUniform(name, color)` is
`canvas.color_uniform`.

## Animation and frames

| Source | Lowered |
|---|---|
| `animate { body }` | `ui.animate(none, closure{body})` |
| `animate(.spring) { body }` | `ui.animate(anim, closure{body})`, anim an `Animation` |
| `animating()` | `ui.animating()` (whether the changes made now animate) |
| `let stop = onFrame(t => …)` | `ui.on_frame(closure(t: Duration))`, which returns the stop closure; the frames stop with the current owner too |

`body` runs synchronously, its writes batched; the props and frames the
next flush changes are sent between AnimateBegin and AnimateEnd.
`Animation.done` is called once the animation is complete.

## Keys, drags, drops, accessibility

`keys: [Shortcut(key: "down", run: () => …)]` and `actions:
[A11yAction(name: "Delete", run: …)]` are style props: any element
takes them (a flattened container gets a view). `useKeys([...])` in a
component is `ui.use_keys(keys)`: the root's keys while the component
lives, the last one's winning. A container's `dragItems: [DragItem]`
(often a binding) makes it draggable and `dropTypes: [string]` a drop
target; their events are `dragPhase`, `dropOver`, `dropMove` and `drop`.
`on adjust(by)` makes a view adjustable for screen readers.

## Media and the system

Commands on a view are builtins taking its ref:

| Source | Lowered |
|---|---|
| `video.play()`, `.pause()`, `.seek(s)` | `ui.play(v)`, `ui.pause(v)`, `ui.seek(v, s)` |
| `web.eval(js)`, `.back()`, `.forward()`, `.reload()`, `.stop()` | `ui.web_eval(v, js)`, `ui.web_navigate(v, 0..3)` |
| `try await web.evalResult(js)`, `try await webCookies(url)` | `ui.web_eval_result(v, js)`, `ui.web_cookies(url)`: AWAIT on the future |
| `field.insertText(s)`, `.format(.bold, on)`, `.toggleFormat(.bold)`, `.setLink(url)` | `ui.insert_text`, `ui.format(v, f, on)`, `ui.toggle_format(v, f)`, `ui.set_link` |
| `share(text)`, `try await shareFiles(paths, ...)`, `haptic(.success)`, `copy(text)`, `openURL(url)`, `announce(text)`, `a11yFocus(view)` | `ui.share`, `ui.share_files`, `ui.haptic`, `ui.copy`, `ui.open_url`, `ui.announce`, `ui.a11y_focus` |
| `onOpenURL(u => …)`, `onOpenFiles(ps => …)`, `onShare(items => …)` | `ui.on_open_url`, `ui.on_open_files`, `ui.on_share`: handlers while the component lives |

A host module's method (one Asuka doesn't wrap) is `ui.call(module,
method, args)`, a future of the answer's bytes.

## Native components

A `native component X(…) { prop …; event … }` of a native package
(docs/native.md) lowers as a component whose body is one native view:

| Source | Lowered |
|---|---|
| making it | `v = ui.native(type)`, type `"<module ID>#X"`, the module ID the platforms' generated code registers (as `native.new`'s cls) |
| a prop, constant | `ui.native_set(v, "name", value, desc)`, desc its type's descriptor (native.h's) |
| a prop, reactive | `ui.native_bind(v, "name", thunk, desc)` |
| `on name(a, b) { … }` | `ui.native_on(v, "name", closure, desc)`, desc its parameters' descriptors, in order |
| the style | `ui.set` / `ui.bind` of style keys on v |

The handlers are set before the props (a view may send an event as a
prop is set); the runtime sends the props after the listening, then each
again only when its encoding changes. Props cross as bytes (native.go's
type 5) in the native codec; an event's values are its one value "v",
decoded parameter by parameter.

## Languages

An app's catalogs compile at build time with package i18n
(`Catalog.Compile`): the blob, the keys sorted, and each key's arguments
in its source message's order. The blob is a module constant; the
module's init makes the catalog global, `i18n.catalog(blob)`, which
registers it: its languages become the app's.

| Source | Lowered |
|---|---|
| `tr.inbox.title(unread: n)` | `i18n.say(catalog, key, (n,), none)`, key the index of `inboxTitle`'s key; the tuple of the arguments in the key's order: an int or a float for number, plural and selectordinal arguments, a string otherwise |
| `tr.in("de").greeting(name: n)` | `i18n.say(catalog, key, (n,), "de")` |
| `locale()`, `setLocale(tag)`, `setLanguages(tags)`, `languages()`, `preferredLanguages()`, `isRTL()`, `region()`, `formatLocale()` | `i18n.locale`, `i18n.set_locale`, ... |

`i18n.say` reads the language shown, tracked: a binding that says a
message follows the language as it follows its arguments. A key missing
in a language falls back as `Catalog.Format` does (its CLDR parent
locales, then the source); a key none has says its name. Dates are the
caller's to write (a `date` or `time` argument takes a string) until the
language has a date type.

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

A `test` block is an async function in Module.Tests. `mount(view)` is
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

`json.decode<T>(text)`, `json.encode(value)` and the network's
`.json<T>()` lower to the builtins `json.decode(text, desc)` and
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

`asuka native generate` writes each platform's side (Swift on iOS and macOS, Kotlin,
JavaScript, Go on Windows and Linux, whose hosts are Go) from the same declarations, in
native/codec.go's wire format.
