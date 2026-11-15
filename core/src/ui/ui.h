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
// an ak_el_class), its keys and events in lang/prelude/elements.go, and
// its behavior ported from the Go file named:
//   - rich text and Selectable (richtext.go, richfield.go): Text's
//     selectable key is ignored until then; a TextField's rich, paste
//     types, formats and accessory (its keyboard accessory: an Accessory
//     provider, which nav.c's fits/full_width layout already sizes).
//   - canvas, shapes and shaders (canvas.go, shape.go, shader.go); video
//     and web views (video.go, web.go); native views (native.go).
//   - gestures (pan, pinch, rotate, swipe), animation (animate.go,
//     frame.go's OnFrame), keys (keys.go), drag and drop and interaction
//     states: the style's Hovered and Pressed variants (interaction.go).
//   - share, haptics, calls (share.go, call.go); the app's menu bar
//     (window.go's MenuBar) and the main window's options
//     (SetWindowOptions); a Window's flyout anchor; the app's language
//     and its catalogs (locale.go: ak_ui_set_direction is its hook for
//     now).
//   - the router (router.c): a split view's routes, the navigation state
//     kept in the history (a browser's back rebuilds from the href),
//     guards followed as what they read changes, NavLink's push and
//     replace modes on a Button.
//   - Image: BlurHash and ThumbHash placeholders, animated images
//     following Reduce Motion (media.go, imagehash.go).
//   - The keep registry of the dev loop: state cells kept across reloads
//     by ak_ui_path_key (below).
#ifndef AK_UI_H
#define AK_UI_H

#include "rt/rt.h"
#include "ops/ops.h"
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
// ak_ui_slot fills a named slot with a view, or (a lazy slot: a
// Popover's content) a closure returning one, built when it shows.
void ak_ui_slot(ak_view *v, uint16_t slot, ak_view *child);
// ak_ui_lazy gives an element whose children are built when they show
// (LazyChildren: a Stack's first screen, a Tab's page, a Sheet's, a
// Window's or a Destination's content, a Route's screen, a Boundary's
// content) the builder of its children: a closure returning a View,
// called with the element's builder parameters (a Route's Params, an
// accessor).
void ak_ui_lazy(ak_view *v, ak_closure *build);
// ak_ui_ref makes v reachable as a value (TextField as field): the
// commands below take it. It returns v, retained.
ak_view *ak_ui_ref(ak_view *v);
// ak_ui_fragment is a view of no native view of its own: a component's
// several views, each added with ak_ui_add.
ak_view *ak_ui_fragment(void);

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
// current owner, under an owner of its own (a root under the stack's),
// where push and pop find that stack, and OnBack the screen: a screen
// declaration's Screen adopts the screen made for it, other views are
// put in one. pop takes the top screen off (not the first), disposing
// it; depth is how many screens the stack holds (0: none). They throw
// outside a Stack.
void ak_ui_push(ak_closure *build);
void ak_ui_pop(void);
int64_t ak_ui_depth(void);
// present shows build()'s view in a sheet, owned by the current owner
// (it closes as that's disposed); dismiss closes the nearest sheet, as
// the user's dismissal does (a Sheet's change(false) and dismiss).
void ak_ui_present(ak_closure *build);
void ak_ui_dismiss(void);
// ak_ui_on_back takes the user's going back from where the component is
// (a pushed screen, a sheet or popover, else the app): handler returns
// true when it took it; the latest registered is asked first, and it
// goes with the current owner.
void ak_ui_on_back(ak_closure *handler);
// ak_ui_alert shows an alert, or an action sheet, now: actions an
// [Action], or a closure returning one that it follows while it shows;
// none: OK. fields ([AlertField], an alert's) are text fields.
void ak_ui_alert(ak_str *title, ak_str *message, ak_obj *actions, bool action_sheet, ak_arr *fields);
// ak_ui_snackbar shows a brief message at the bottom of the window now,
// with an optional Action (its title and run), for duration
// (AK_SNACKBAR_DURATION_*), and returns a closure that dismisses it
// (owned). on_dismiss runs when it goes other than by its action.
ak_closure *ak_ui_snackbar(ak_str *message, const ak_enum *action, int64_t duration, ak_closure *on_dismiss);

// Windows (desktop): w is a Window element (Window(…) as w), or NULL:
// the window the calling code is in, else the main window.
void ak_ui_window_action(ak_view *w, int64_t action, float x, float y);  // AK_WINDOW_ACTION_*
void ak_ui_window_close(ak_view *w);  // now, without asking
ak_val ak_ui_window_state(ak_view *w);  // a WindowState, read (followed)
void ak_ui_on_close_request(ak_closure *handler);  // fn() -> bool: false keeps it open
extern const ak_shape ak_ui_shape_window_state;

// The Router the calling code is in (else the main window's first):
// navigate goes to href (mode 0 navigate, 1 push, 2 replace); back and
// dismiss report whether there was somewhere to go; router_href is the
// address shown, read (followed). href_of fills a pattern's parameters.
void ak_ui_navigate(ak_str *href, int64_t mode);
bool ak_ui_router_back(void);
bool ak_ui_router_dismiss(void);
bool ak_ui_can_go_back(void);
void ak_ui_set_query(ak_str *query);
ak_str *ak_ui_router_href(void);  // owned
ak_str *ak_ui_href_of(ak_str *pattern, ak_arr *values);  // owned; throws on a count that isn't the pattern's
extern const ak_shape ak_ui_shape_params;

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
