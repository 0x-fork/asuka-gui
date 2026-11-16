// ui.h: the UI runtime. A port of the Go ui package to C: the node tree
// over native views, the flush (effects, then one batch of props, layout,
// one batch of frames), the built-in elements' behavior (controls, lists,
// text input, ...), events from the host.
//
// Compiled code (the VM's builtins, core/src/ui/builtins.c; C from
// lang/cgen) talks to it through one protocol, the same for every
// element:
//
//	v = ak_ui_new(AK_EL_BUTTON)              an element, owned by the current owner
//	ak_ui_set(v, AK_KEY_BUTTON_TITLE, val)   a key set once
//	ak_ui_bind(v, AK_KEY_COLOR, thunk)       a key that follows a closure (a render effect)
//	ak_ui_on(v, AK_EV_BUTTON_PRESS, handler) an event handler
//	ak_ui_add(v, child)                      a child (an element or a fragment)
//	ak_ui_slot(v, AK_KEY_<EL>_<SLOT>, w)     a named slot
//
// The elements, their keys (props), events and slots are declared once,
// in lang/prelude/elements.go; go generate writes elements_gen.h (the
// AK_EL_, AK_KEY_ and AK_EV_ constants and the tables) and the
// compiler's prelude from it. A key may be a wire prop of the element's
// native view, a style field (layout or look), or a behavior the runtime
// implements (a text field's two-way text, a list's rows). A slot is a
// key of kind View.
//
// Control flow makes fragments: views with no native view whose children
// splice into their parent's. A View, Row or Column with no look, events
// or accessibility props makes no native view either (it's flattened):
// its children are placed in the nearest real ancestor, offset by its
// frame.
//
// Values cross as rt values (rt.h), by lang/LOWERING.md's "Types to
// kinds": a key's kind is ak_key_kinds[key]. Structs (Edges, Font,
// Transform, ...) are ak_structs whose fields are in types.asuka's order.
// Handlers are closures called with the event's parameters (borrowed);
// they return nothing, or (an async handler) the future of the task they
// started, which the runtime releases: either way a non-NULL result is
// owned and released.
//
// Memory: a view is reference counted (an rt object). Its parent holds
// its children; compiled code holds what's in its registers. A view's
// native view is destroyed when its parent removes it (a Show switching
// branches, a For's row going), when its root unmounts, or, for one that
// never got a parent, when the last reference to it goes.
//
// Main thread only.
//
// ---- Deferred to the next job (the structure supports each) ----
//
// The rest of the Go ui package. Each is an element class (core/src/ui:
// an ak_el_class in elements.c), its keys and events in
// lang/prelude/elements.go, and its behavior ported from the Go file named:
//   - navigation: Stack, Screen (title, bar buttons, toolbar slot), Tabs,
//     Sheet, Popover, Alert, Menu, the router and hrefs (nav.go, route.go,
//     router.go, back.go): host-sized layout roots with their own
//     environment (the element's host_sized and env fields are there for
//     them; layout() lays them out); ak_ui_push/pop/present/dismiss below.
//     A Button's href is stored and ignored until the router lands.
//   - rich text and Selectable (richtext.go, richfield.go): Text's
//     selectable key is ignored until then; a TextField's rich, paste
//     types, formats and accessory.
//   - canvas, shapes and shaders (canvas.go, shape.go, shader.go); video
//     and web views (video.go, web.go); native views (native.go).
//   - gestures (pan, pinch, rotate, swipe), animation (animate.go,
//     frame.go's OnFrame), keys (keys.go), drag and drop and interaction
//     states: the style's Hovered and Pressed variants (interaction.go).
//   - windows, split views, snackbars, keyboard accessories (window.go,
//     windowhandle.go, split.go, snackbar.go); share, haptics, calls
//     (share.go, call.go); the app's language and its catalogs
//     (locale.go: ak_ui_set_direction is its hook for now).
//   - List: sections and headers, swipe actions, selection (list.go's
//     Section, SwipeActions, Selecting); Scroll: paging, page control.
//   - Image: BlurHash and ThumbHash placeholders, animated images
//     following Reduce Motion (media.go, imagehash.go).
//   - Error boundaries (control.go's Catch) as an element over ak_catch.
//   - The keep registry of the dev loop: state cells kept across reloads
//     by ak_ui_path_key (below).
#ifndef AK_UI_H
#define AK_UI_H

#include "rt/rt.h"
#include "ops/ops.h"
#include "rt/json.h"
#include "ui/elements_gen.h"

typedef struct ak_view ak_view;  // an rt object (AK_TAG_VIEW)

// ---- The host ----

typedef struct ak_host {
	void *ctx;
	// apply applies an encoded batch (ops/ops.go's format).
	void (*apply)(void *ctx, const uint8_t *buf, size_t len);
	// measure measures view id: w, h known sizes or NaN; aw, ah the space
	// available (definite, +inf max-content, -inf min-content).
	void (*measure)(void *ctx, uint32_t id, float w, float h, float aw, float ah, float *ow, float *oh);
} ak_host;

// ak_ui_init sets the host roots are mounted on (and binds the signal
// graph's flush to the runtime's). Again with another host: roots
// mounted later go there.
void ak_ui_init(const ak_host *host);
// ak_ui_shutdown unmounts what's mounted and lets the runtime's memory
// go (tests, a dev player switching apps).
void ak_ui_shutdown(void);

// ---- Roots ----

// ak_ui_mount builds the app's root view by calling build (a closure
// returning a View) under a new root owner, and makes it the window's
// root. ak_ui_unmount disposes it: its effects stop, its views are
// destroyed.
void ak_ui_mount(ak_closure *build);
void ak_ui_unmount(void);
bool ak_ui_mounted(void);

// The environment: what the host reports (an Environment event) about
// the root view: its size, safe area, keyboard, scale, appearance, text
// size, languages. One reported before the root mounts is the one it
// mounts in. ak_ui_set_environment is the same without an event.
void ak_ui_set_environment(const ak_environment *env);
// ak_ui_environment fills env (its strings borrowed) with the root's.
void ak_ui_environment(ak_environment *env);

// ak_ui_set_direction lays the app out right to left (or not): its
// layout roots' direction, the start and end sides, a horizontal
// Scroll's. The app's language decides it (locale.go's job, deferred);
// ASUKA_DIRECTION=rtl forces it at init.
void ak_ui_set_direction(bool rtl);

// From the host (the C ABI, core/src/abi):
void ak_ui_dispatch(const uint8_t *event, size_t len);
int64_t ak_ui_list_count(uint32_t list);
void ak_ui_list_bind(uint32_t list, int64_t index, uint32_t cell);
void ak_ui_list_unbind(uint32_t list, uint32_t cell);
// ak_ui_run_posted runs what other threads posted and the tasks whose
// awaits completed, in a batch, then flushes (asuka_run_posted).
void ak_ui_run_posted(void);
// ak_ui_flush runs pending effects and sends what changed. The runtime
// flushes after each event and posted callback; call it after anything
// else that writes state outside an event (tests).
void ak_ui_flush(void);
// ak_ui_reattach tells the host again which view is the root and resends
// every frame (an Android activity recreated).
void ak_ui_reattach(void);
// ak_ui_remeasure forgets what the host measured and measures again
// (fonts loaded, a player that came back).
void ak_ui_remeasure(void);

// ---- Elements ----

ak_view *ak_ui_new(uint16_t el);
void ak_ui_set(ak_view *v, uint16_t key, ak_val value);   // borrows value
void ak_ui_bind(ak_view *v, uint16_t key, ak_closure *thunk);
void ak_ui_on(ak_view *v, uint16_t event, ak_closure *handler);
void ak_ui_add(ak_view *v, ak_view *child);  // child NULL: nothing
void ak_ui_slot(ak_view *v, uint16_t slot, ak_view *child);
// ak_ui_ref makes v reachable as a value (TextField as field): the
// commands below take it. It returns v, retained.
ak_view *ak_ui_ref(ak_view *v);
// ak_ui_fragment is a view of no native view of its own: a component's
// several views, each added with ak_ui_add.
ak_view *ak_ui_fragment(void);

// A package's native component (native_view.c): a Native view of kind
// ("<class>#<Name>"), its props set or bound by name, encoded by their
// descriptors (native/native.h), and its events handled by name, their
// values decoded by desc. t: the module's types (its structs' shapes).
ak_view *ak_ui_native(const ak_str *kind);
void ak_ui_native_set(ak_view *v, const ak_str *name, ak_val value, const ak_str *desc, const ak_json_types *t);
void ak_ui_native_bind(ak_view *v, const ak_str *name, ak_closure *thunk, const ak_str *desc, const ak_json_types *t);
void ak_ui_native_on(ak_view *v, const ak_str *name, ak_closure *handler, const ak_str *desc, const ak_json_types *t);

// The native view's ID (0: none: a fragment, a flattened container).
uint32_t ak_ui_id(const ak_view *v);

// ---- Control flow (fragments) ----

// ak_ui_show shows then() while cond() is true, otherwise() (or nothing:
// NULL) while it's false. Each branch is built under its own owner,
// disposed when the branch goes.
ak_view *ak_ui_show(ak_closure *cond, ak_closure *then, ak_closure *otherwise);
// ak_ui_for keeps a row per key of items() (an array): key(item) is the
// key, of kind key_kind (NULL: the row's position is its key); row(item,
// index) builds it, where item and index are accessors (AGET) that follow
// the key's value and position. A row is built under its own owner,
// disposed when its key goes; a moved row moves its native views.
ak_view *ak_ui_for(ak_closure *items, ak_closure *key, ak_closure *row, ak_kind key_kind);
// ak_ui_switch builds build(case) for the case selector() returns (an
// int), again whenever it changes (a match in a view).
ak_view *ak_ui_switch(ak_closure *selector, ak_closure *build);
// ak_ui_dynamic rebuilds build() whenever what it read changes, even in
// what it calls.
ak_view *ak_ui_dynamic(ak_closure *build);
// ak_ui_lazy_for is a for directly in an element that builds its
// children lazily (a List): rows exist only while the host shows them.
void ak_ui_lazy_for(ak_view *v, ak_closure *items, ak_closure *key, ak_closure *row, ak_kind key_kind);

// ---- Components: contexts, tasks, instance paths ----

// ak_ui_provide makes value (retained, released with the owner) what
// ak_ui_use(key) finds under the current owner. Keys are names (a type's).
void ak_ui_provide(ak_str *key, ak_val value);
ak_val ak_ui_use(ak_str *key);  // retained; NULL when none is provided
// ak_ui_task starts c (an async closure) once the component it's in is
// built, owned by the current owner: disposing it cancels the task.
void ak_ui_task(ak_closure *c);
// ak_ui_load makes a loader (let x = load f(…)): thunk (tracked) starts
// the call as a task and returns its future; it runs again, cancelling
// the last task, when what it read changes or on ak_ui_load_reload. The
// state (0 pending, 1 ready, 2 failed), the latest value (boxed when a
// scalar; none before the first) and the error are tracked reads.
ak_val ak_ui_load(ak_closure *thunk);  // owned
int64_t ak_ui_load_state(ak_val loader);
ak_val ak_ui_load_value(ak_val loader);  // owned
ak_val ak_ui_load_error(ak_val loader);  // owned
void ak_ui_load_reload(ak_val loader);

// Instance paths. A state cell kept across reloads (CELLK) is found by
// its key (Component.x) and the place of the component instance that
// made it: the instance path. The compiler emits ak_ui_enter(name) at
// the start of a component's body and ak_ui_leave() at its end; the
// runtime adds what control flow builds: a Show's branch, a For's row
// (its key), a Switch's case, a Dynamic's build. A component used twice
// in the same place is told apart by its count there:
//
//	App#0/Inbox#0/?then/[42]/MessageRow#0/Avatar#1
//
// ak_ui_path_key is key under the current path (what the keep registry
// stores a cell by).
void ak_ui_enter(ak_str *name);
void ak_ui_leave(void);
ak_str *ak_ui_path_key(ak_str *key);  // owned
ak_str *ak_ui_path(void);             // owned: the current path

// ---- The environment, to compiled code ----

// ak_ui_environment_value is the root's environment as an Environment
// struct (types.asuka; ak_ui_shape_environment): reading it in a
// binding follows it.
ak_val ak_ui_environment_value(void);
extern const ak_shape ak_ui_shape_environment;
extern const ak_shape ak_ui_shape_text_range;
extern const ak_shape ak_ui_shape_frame;

// ---- Commands and navigation ----

void ak_ui_focus(ak_view *v);
void ak_ui_blur(ak_view *v);
void ak_ui_scroll_to(ak_view *v, float x, float y, bool animated);
// push builds the screen build() returns on the stack nearest the
// current owner; pop takes the top one off; present shows build()'s view
// in a sheet; dismiss closes the nearest sheet. Deferred with navigation:
// they throw "not available yet" for now.
void ak_ui_push(ak_closure *build);
void ak_ui_pop(void);
void ak_ui_present(ak_closure *build);
void ak_ui_dismiss(void);

// ---- Builtins ----

// ak_ui_builtins_init registers the ui.* builtins (builtins.c) with the
// VM; ak_vm_init doesn't, so a VM without a UI runs without one.
void ak_ui_builtins_init(void);

// ---- Introspection (tests, the dev loop) ----

typedef struct {
	uint64_t flushes;
	uint64_t measures;       // measure calls made to the host
	uint64_t measure_hits;   // measurements the shared cache answered
	uint32_t views;          // live views (rt objects), every root's
	uint32_t native;         // native views made and not destroyed
} ak_ui_stats;
void ak_ui_get_stats(ak_ui_stats *s);

#endif
