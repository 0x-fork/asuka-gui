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
| children of a `LazyChildren` element (`Stack`, `Tab`, `Sheet`, `Destination`, `Window`, `Route`, `Boundary`) | `ui.lazy(v, builder)`: the runtime builds them when they show, under an owner of their own (a `Route`'s builder takes its `Params`, an accessor) |
| `toolbar { … }` | `ui.slot(v, AK_KEY_SCREEN_TOOLBAR, fragment)` |
| a `Lazy` slot (`Popover`'s `content { … }`) | `ui.slot(v, key, builder)`: a closure returning the View, built as it shows |
| `on error(e, reset) { views }` (an event that `Builds`) | `ui.on(v, ev, closure)`, the closure returning the View it builds |
| `Text(x) as t` | `t` is a local holding `ui.ref(v)` |

A `Screen`'s `actions`, `toolbar` and `primary` slots hold Buttons:
the runtime sends them as the platform's bar items (title or icon, a
`menu`, `disabled`), and runs a Button's `press` (and its `href`) when
its item is chosen; a Button that changes, or one a `for` or an `if` in
the slot adds, sends the bar again.

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
taking the ref. The rest (core/src/ui/builtins.c has their arguments):

| Source | Lowered |
|---|---|
| `onBack { … return true }` (a component's statement) | `ui.on_back(closure)`: the closure returns whether it took the user's going back from the nearest pushed screen, sheet or popover (else the app's); it goes with the component |
| `depth()` | `ui.depth()`: the nearest stack's screens |
| `alert("Delete?", message: m, actions: [.button(…)], fields: […])` | `ui.alert(title, message, actions, actionSheet, fields)`; `actions` a closure when they read state (a button disabled while a field is empty), followed while it shows |
| `snackbar("Deleted", action: .button("Undo") { … })` | `ui.snackbar(message, action, duration, onDismiss)`, returning the closure that dismisses it |
| `w.show()`, `w.setSize(w, h)`, … on `Window(…) as w` (none: the window it's in) | `ui.window_action(w, action, x, y)`; `w.close()` `ui.window_close(w)`; `w.state` `ui.window_state(w)`, followed |
| `onCloseRequest { … return ok }` | `ui.on_close_request(closure)` |
| `navigate(href)`, `navigate(X(…))` | `ui.navigate(href, mode)` (0 navigate, 1 push, 2 replace); `X(…)` a routed screen (below): its href |
| `router.back()`, `router.dismiss()`, `router.canGoBack`, `router.href`, `router.setQuery(q)` | `ui.router_back`, `ui.router_dismiss`, `ui.can_go_back`, `ui.router_href` (followed), `ui.set_query` |

## Routes

A `Router` maps addresses to screens, as the Go ui's did: every screen
of it has an href (`/inbox/18`, `/search?q=go`), which the web shows
in its address bar, a link opens, and a reload comes back to. Routes
are elements, so they're declared where the app's structure is:

```asuka
app {
  Router(initial: "/inbox") {
    Route("/", redirect: "/inbox")
    Route(layout: .tabs) {
      Route("/inbox", layout: .stack, title: "Inbox", icon: "tray") {
        Inbox                                    // its screen's own path: ""
        Thread                                   // ":id"
        Route(":id/reply", sheet: true) { (p) in Reply(id: p.get("id")) }
      }
      Route("/settings", layout: .stack, title: "Settings", icon: "gear") {
        Settings
      }
    }
    Route("/admin", layout: .stack, guard: { to in signedIn ? "" : "/login?next={to}" }) { Users }
  }
}

screen Inbox at "" { … }
screen Thread(id: int) at ":id" { … }    // id: from the path, parsed
```

- `Router(initial:) { … }` is `ui.new(AK_EL_ROUTER)`, its keys, then
  `ui.add` of each `Route`; a `Route` with routes in it likewise. A
  route's layout makes it a navigator of its children (`.stack`,
  `.tabs`, `.split`, `.slot`), `.group` (the default) only gathers them
  under its path and guard.
- `Route(path) { (p) in View }` is `ui.lazy(route, closure(pAcc){…})`:
  the runtime builds it for each screen of the route, with its `Params`
  (an accessor: a kept screen follows its query, `setQuery`).
- A screen declared with `at "pattern"` is routable: named in a
  `Route`'s block, it's a `Route` of its pattern (relative to the
  parent's, unless it starts with `/`) whose builder reads each of the
  screen's parameters from the path's parameters, else the query's,
  converted to its type (`int`, `float`, `bool`, an enum's case name,
  `string`); a value that doesn't convert throws `RouteError` to the
  nearest `Boundary`. Its title is the screen's own.
- `navigate(Thread(id: 7))`, a Button's `href: Thread(id: 7)`: the
  compiler knows Thread's full pattern from the one Router that routes it
  (`/inbox/:id`) and lowers the href to `ui.href_of("/inbox/:id",
  ["7"])`; a screen two Routers route, or none, is an error there. A
  string href is checked against the routes when it's a constant.
- Guards are closures `fn(string) -> string` (the href asked for; `""`
  lets the user in, an href sends them there), asked as the router
  navigates. Redirects fill the route's parameters into theirs.
- The router is the nearest one around the code (a context the router
  gives what it builds), else the main window's first.

The runtime (core/src/ui/router.c) is the Go ui's router: the navigation
state described, then reconciled; Location commands with how the
history moved; a browser's back and forward (the Location event); links
the app is opened with.

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
