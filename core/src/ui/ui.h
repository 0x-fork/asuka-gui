// ui.h: the UI runtime. A port of the Go ui package to C: the node tree
// over native views, the flush (effects, then one batch of props, layout,
// one batch of frames), the built-in elements' behavior (controls, lists,
// navigation, sheets, alerts, menus, rich text, ...), events from the
// host.
//
// Compiled code (the VM's builtins, C from lang/cgen) talks to it through
// one protocol, the same for every element:
//
//	v = ak_ui_new(AK_EL_BUTTON)              an element, owned by the current owner
//	ak_ui_set(v, AK_KEY_BUTTON_TITLE, val)   a key set once
//	ak_ui_bind(v, AK_KEY_COLOR, thunk)       a key that follows a closure (a render effect)
//	ak_ui_on(v, AK_EV_BUTTON_PRESS, handler) an event handler
//	ak_ui_add(v, child)                      a child (an element or a fragment)
//	ak_ui_slot(v, AK_SLOT_SCREEN_TOOLBAR, w) a named slot
//
// The elements, their keys (props), events and slots are declared once,
// in lang/prelude/elements.go; go generate writes elements_gen.h (the
// AK_EL_, AK_KEY_, AK_EV_ and AK_SLOT_ constants and the tables) and the
// compiler's prelude from it. A key may be a wire prop of the element's
// native view, a style field (layout or look), or a behavior the runtime
// implements (a text field's two-way text, a list's selection).
//
// Control flow makes fragments: views with no native view whose children
// splice into their parent's.
//
// Main thread only.
#ifndef AK_UI_H
#define AK_UI_H

#include "rt/rt.h"
#include "ui/elements_gen.h"

typedef struct ak_view ak_view;  // an rt object (AK_T_VIEW)

// ---- The host ----

typedef struct ak_host {
	void *ctx;
	// apply applies an encoded batch (ops/ops.go's format).
	void (*apply)(void *ctx, const uint8_t *buf, size_t len);
	// measure measures view id: w, h known sizes or NaN; aw, ah the space
	// available (definite, +inf max-content, -inf min-content).
	void (*measure)(void *ctx, uint32_t id, float w, float h, float aw, float ah, float *ow, float *oh);
} ak_host;

void ak_ui_init(const ak_host *host);

// ---- Roots ----

// ak_ui_mount builds the app's root view by calling build (a closure
// returning a View) under a new root owner, and makes it the window's
// root. ak_ui_unmount disposes it.
void ak_ui_mount(ak_closure *build);
void ak_ui_unmount(void);

// From the host (the C ABI, core/src/abi):
void ak_ui_dispatch(const uint8_t *event, size_t len);
int64_t ak_ui_list_count(uint32_t list);
void ak_ui_list_bind(uint32_t list, int64_t index, uint32_t cell);
void ak_ui_list_unbind(uint32_t list, uint32_t cell);
// ak_ui_flush runs pending effects and sends what changed. The runtime
// flushes after each event and posted callback; call it after anything
// else that writes state outside an event (tests).
void ak_ui_flush(void);

// ---- Elements ----

ak_view *ak_ui_new(uint16_t el);
void ak_ui_set(ak_view *v, uint16_t key, ak_val value);   // borrows value
void ak_ui_bind(ak_view *v, uint16_t key, ak_closure *thunk);
void ak_ui_on(ak_view *v, uint16_t event, ak_closure *handler);
void ak_ui_add(ak_view *v, ak_view *child);
void ak_ui_slot(ak_view *v, uint16_t slot, ak_view *child);
// ak_ui_ref makes v reachable as a value (TextField as field): the
// commands below take it.
ak_view *ak_ui_ref(ak_view *v);

// ---- Control flow (fragments) ----

// ak_ui_show shows then() while cond() is true, otherwise() (or nothing)
// while it's false. Each branch is built under its own owner, disposed
// when the branch goes.
ak_view *ak_ui_show(ak_closure *cond, ak_closure *then, ak_closure *otherwise);
// ak_ui_for keeps a row per key of items() (an array): row(item, index)
// builds it, where item and index are accessors that follow the key's
// value and position.
ak_view *ak_ui_for(ak_closure *items, ak_closure *key, ak_closure *row);
// ak_ui_switch builds build(case) for the case selector() returns,
// again whenever it changes (a match in a view).
ak_view *ak_ui_switch(ak_closure *selector, ak_closure *build);
// ak_ui_dynamic rebuilds build() whenever what it read changes.
ak_view *ak_ui_dynamic(ak_closure *build);
// ak_ui_lazy_for is a for directly in an element that builds its
// children lazily (a List): rows exist only while on screen.
void ak_ui_lazy_for(ak_view *v, ak_closure *items, ak_closure *key, ak_closure *row);

// ---- Commands and navigation ----

void ak_ui_focus(ak_view *v);
void ak_ui_blur(ak_view *v);
void ak_ui_scroll_to(ak_view *v, float x, float y, bool animated);
// push builds the screen build() returns on the stack nearest the
// current owner; pop takes the top one off; present shows build()'s view
// in a sheet; dismiss closes the nearest sheet.
void ak_ui_push(ak_closure *build);
void ak_ui_pop(void);
void ak_ui_present(ak_closure *build);
void ak_ui_dismiss(void);

#endif
