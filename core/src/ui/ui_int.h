// ui_int.h: the UI runtime's insides, shared by its files.
//
//	element.c   the node tree: views, fragments, flattening, native
//	            children, frames, destroying
//	root.c      roots, the flush, layout, events, the environment
//	style.c     the style keys: layout and look
//	measure.c   the shared measure cache
//	elements.c  the built-in elements' classes
//	control.c   Show, For, Switch, Dynamic; contexts, tasks
//	list.c      List: rows bound by the host
//	path.c      instance paths
//	builtins.c  the VM's ui.* builtins
#ifndef AK_UI_INT_H
#define AK_UI_INT_H

#include "ui/ui.h"
#include "signal/signal.h"
#include "layout/layout.h"
#include "rt/sched.h"
#include "vm/vm.h"

typedef struct ak_ui_root ak_ui_root;
typedef struct ak_el_class ak_el_class;

// A style key set on a view (its value retained when a reference).
typedef struct {
	uint16_t key;
	ak_val v;
} ak_style_ent;

// An event handler.
typedef struct {
	uint16_t ev;  // AK_EV_*
	ak_closure *c;
} ak_handler_ent;

// Class flags.
enum {
	AK_ELF_FLATTEN = 1 << 0,   // a container that makes no view until it needs one
	AK_ELF_TEXT = 1 << 1,      // draws text: takes font, lines, textAlign
	AK_ELF_MEASURED = 1 << 2,  // the host measures it
	AK_ELF_CACHE = 1 << 3,     // its size is a function of its content: the shared cache
	AK_ELF_CHILDREN = 1 << 4,  // takes children
	AK_ELF_FRAGMENT = 1 << 5,
};

// An element's class: what's particular to a kind of element.
struct ak_el_class {
	const char *name;
	ak_view_kind kind;  // its native view's
	uint16_t flags;
	// init sets up a new view (its state, what it always sends).
	void (*init)(ak_view *e);
	// set applies one of the element's own keys (not the style's), which
	// changed: prev is what it was, NULL the first time it's set.
	void (*set)(ak_view *e, uint16_t key, ak_val v, const ak_val *prev);
	// finalize runs once its keys are set: as it's added to a parent,
	// mounted, or at the next flush.
	void (*finalize)(ak_view *e);
	// event handles an event from the host.
	void (*event)(ak_view *e, const ak_event *ev);
	// adjust changes the layout style the view's style makes.
	void (*adjust)(ak_view *e, ak_lstyle *ls);
	// add places a child (a Scroll's in its content); NULL: the view's own.
	void (*add)(ak_view *e, ak_view *child);
	// style_target is the view a style key goes to (a Scroll's content
	// for what lays its children out), NULL: none; NULL hook: e itself.
	ak_view *(*style_target)(ak_view *e, uint16_t key);
	// styled hears of a style key set on e (a List's gap).
	void (*styled)(ak_view *e, uint16_t key);
	// drop frees the class's state.
	void (*drop)(ak_view *e);
};

struct ak_view {
	ak_opaque h;  // tag AK_TAG_VIEW
	ak_ui_root *r;
	const ak_el_class *cls;
	uint16_t el;  // AK_EL_*, 0 for the runtime's own views
	ak_view_kind kind;
	ak_id id;      // 0 until it has a native view
	uint8_t dir;   // AK_LAYOUT_DIRECTION_*: its own, or a parent's it took
	bool real;     // it has a native view
	bool frag;     // a fragment: no box, no view
	bool cell;     // a list cell: the host made its view
	bool dead;     // its views are destroyed
	bool finalized;
	bool kids_dirty, frame_q, layout_q;
	bool measured;  // the host measures it (ContentInset, sizes rounded up)
	bool fill;      // the root's child: grows into the screen
	bool host_sized;
	bool safe;      // a SafeArea: extra_pad follows the safe area
	bool autofocus; // takes the keyboard once it's placed (its first frame)
	ak_view *parent;  // not retained: the parent holds its children
	ak_view *up;      // not retained: where a view of no parent shows (a cell's list)
	AK_VEC(ak_view *) kids;  // retained
	ak_lnode *box;           // NULL for fragments
	uint32_t native_kids;    // native children inserted into its view
	float sent[4];           // the last SetFrame
	float insets[4];         // ContentInset, as last sent
	float extra_pad[4];      // a SafeArea's insets
	AK_VEC(ak_style_ent) style;
	AK_VEC(ak_handler_ent) handlers;
	uint64_t listens;  // the ops events it listens for, a bit each
	ak_owner *owner;   // retained: the owner it was made under
	ak_str *text, *text2;  // what the measure cache keys it by (Text's text; Button's title and icon)
	int32_t variant;
	void *impl;  // the class's state
	// A grid's tracks, converted for layout (what its style points to).
	void *grid_mem;
};

// The shared measure cache: two generations of up to 1024 entries,
// keyed by content.
typedef struct {
	ak_view_kind kind;
	int32_t variant;
	ak_str *text, *text2, *family;  // retained
	int32_t font_style, font_weight;
	float font_size;
	int64_t lines;
	float font_scale, scale;
	bool inset;
	uint32_t c[4];  // the constraints' bits
} ak_mkey;

typedef struct {
	ak_mkey k;
	uint32_t hash;  // 0: empty
	float w, h;
} ak_mentry;

#define AK_MCACHE_MAX 1024
#define AK_MCACHE_SLOTS 2048

typedef struct {
	ak_mentry *cur, *old;  // AK_MCACHE_SLOTS each
	uint32_t ncur, nold;
	uint64_t hits;
} ak_mcache;

void ak_mcache_init(ak_mcache *m);
void ak_mcache_clear(ak_mcache *m);
void ak_mcache_free(ak_mcache *m);
bool ak_mcache_get(ak_mcache *m, const ak_mkey *k, uint32_t hash, float *w, float *h);
void ak_mcache_put(ak_mcache *m, const ak_mkey *k, uint32_t hash, float w, float h);
uint32_t ak_mkey_hash(const ak_mkey *k);

typedef AK_VEC(ak_view *) ak_view_vec;

struct ak_ui_root {
	uint32_t rc;  // its views, and being mounted
	ak_host host;
	ak_batch *batch;            // where ops go
	AK_VEC(ak_batch *) spare;   // for ops written while one is applied
	ak_view *el;                // its root view
	ak_owner *owner;
	bool disposed;
	// The environment, its strings owned.
	ak_environment env;
	ak_str *env_locale, *env_tz, *env_langs;
	ak_node env_node;
	float scale;
	ak_lsize size;  // as last laid out
	ak_lpass pass;
	ak_view_vec kids_q, frame_q, init_q, layout_q, lists;
	AK_VEC(ak_lnode *) boxbuf;
	ak_mcache measures;
	int laying;
	uint64_t flushes;
	bool flushing;
};

// The runtime's state, one per process.
typedef struct {
	ak_host host;
	bool inited;
	ak_ui_root *root;      // the mounted root (one for now; nothing assumes it)
	ak_ui_root *cur;       // the root views are being made for
	// Views by native ID: open addressing, linear probing.
	ak_view **ids;
	uint32_t ids_cap, ids_len;
	ak_id next_id;
	bool rtl;
	// An environment reported before a root mounted.
	ak_environment pending_env;
	ak_str *pending_strs[3];
	bool has_env;
	ak_ui_stats stats;
	int depth;  // dispatches and flushes in progress
} ak_ui_state;

extern ak_ui_state ak_ui;

// ---- element.c ----

ak_view *ak_view_new(ak_ui_root *r, const ak_el_class *cls, uint16_t el, bool real);
ak_view *ak_frag_new(void);
void ak_view_make_view(ak_view *e);  // gives it a native view
void ak_view_finalize(ak_view *e);
void ak_view_append(ak_view *e, ak_view *k);           // retains k
void ak_view_remove(ak_view *e, ak_view *k, bool destroy);  // releases k
void ak_view_attach_native(ak_view *e);
void ak_view_detach_native(ak_view *e);
void ak_view_destroy(ak_view *e);
void ak_view_mark_kids(ak_view *e);
void ak_view_relink(ak_view *e);
void ak_view_queue_frame(ak_view *e);
void ak_view_emit_frame(ak_view *e);
void ak_view_send_insets(ak_view *e);
void ak_view_make_real(ak_view *e);
void ak_view_remeasure(ak_view *e);
void ak_view_mark_measured(ak_view *e);
void ak_view_measure(ak_view *e);  // the host measures it
ak_view *ak_view_native_parent(const ak_view *e);
void ak_view_each_native(ak_view *e, void (*f)(ak_view *d, void *ctx), void *ctx);
void ak_view_set_dir(ak_view *e, uint8_t dir);
// ak_view_inherit_dir gives k, placed in p, the direction p has.
void ak_view_inherit_dir(ak_view *p, ak_view *k);
uint8_t ak_view_ldir(const ak_view *e);  // its box's direction
bool ak_view_rtl(const ak_view *e);
void ak_view_listen(ak_view *e, ak_event_kind ev);
ak_closure *ak_view_handler(const ak_view *e, uint16_t ev);
// ak_view_call calls e's handler for ev, if any, with args, under e's
// owner: an error it throws goes to the owner's catcher.
void ak_view_call(ak_view *e, uint16_t ev, ak_val *args, uint32_t nargs);
// ak_view_call_closure is the same for a handler of its own (c may be
// NULL: nothing).
void ak_view_call_closure(ak_view *e, ak_closure *c, ak_val *args, uint32_t nargs);
void ak_view_put_id(ak_view *e);
void ak_view_drop_id(ak_id id);
ak_view *ak_view_of(ak_id id);
// The batch views write to.
static inline ak_batch *ak_view_batch(const ak_view *e) { return e->r->batch; }

// ---- style.c ----

// ak_style_set applies a style key (a layout field, a look, a text prop).
void ak_style_set(ak_view *e, uint16_t key, ak_val v);
const ak_val *ak_style_get(const ak_view *e, uint16_t key);  // NULL: unset
// ak_style_layout derives the view's layout style and gives it to its box.
void ak_style_relayout(ak_view *e);
void ak_style_layout(ak_view *e, ak_lstyle *out);
// ak_style_send sends the look of every key set: a view made real.
void ak_style_send_all(ak_view *e);
void ak_style_free(ak_view *e);
bool ak_style_logical(const ak_view *e);
// Lengths, as LOWERING.md packs them.
ak_llength ak_length(int64_t packed);
int64_t ak_length_pt(float v);
int64_t ak_length_pct(float v);
float ak_style_float(const ak_view *e, uint16_t key, float def);
int64_t ak_style_int(const ak_view *e, uint16_t key, int64_t def);

// ---- root.c ----

ak_ui_root *ak_ui_current(void);
void ak_root_retain(ak_ui_root *r);
void ak_root_release(ak_ui_root *r);
void ak_root_flush(ak_ui_root *r);
void ak_root_apply(ak_ui_root *r);
float ak_view_scale(const ak_view *e);
// ak_with_root makes r the root views are made for while fn runs.
void ak_with_root(ak_ui_root *r, void (*fn)(void *ctx), void *ctx);
// ak_ui_raise delivers an error a handler or a builder left pending to
// the catcher of owner (the default handler with none).
void ak_ui_raise_pending(ak_owner *owner);
// ak_ui_build calls build (a closure returning a View) with args, for
// root r, and returns the view (owned, finalized), or NULL (none, or an
// error, which it raised to the current owner's catcher).
ak_view *ak_ui_build(ak_ui_root *r, ak_closure *build, ak_val *args, uint32_t nargs);
void ak_ui_env_strings(ak_environment *dst, ak_str **strs, const ak_environment *src);

// ---- elements.c ----

const ak_el_class *ak_el_class_of(uint16_t el);
extern const ak_el_class ak_class_view, ak_class_frag, ak_class_cell;
// The ops event each AK_EV_ is reported by.
ak_event_kind ak_ev_ops(uint16_t ev);
// Encoders shared by the elements.
void ak_encode_strings(ak_buf *b, const ak_arr *a);
// Pull to refresh, a Scroll's and a List's: *shown is whether the host
// shows its spinner.
void ak_refresh_set(ak_view *e, bool *shown, ak_prop refreshing, ak_val v);
void ak_refresh_event(ak_view *e, bool *shown, ak_prop refreshing, uint16_t key, uint16_t ev);
bool ak_ui_eq(ak_val a, ak_val b, ak_kind k);

// ---- control.c ----

typedef struct ak_path ak_path;
// The for's reconciliation, shared with List: rows by key.
typedef struct {
	ak_val key;
	uint32_t hash;
	void *cell_item;   // the item's cell (vm), and its index's
	void *cell_index;
	ak_view *el;       // retained
	ak_owner *owner;
	int64_t old;
} ak_row;

// ak_row_make builds row (row(itemAcc, indexAcc)) for item at index i,
// under an owner of its own and path's segment of its key; ak_row_update
// gives a kept row its item and index; ak_row_free disposes it (its view
// is its parent's to remove).
void ak_row_make(ak_row *rw, ak_ui_root *r, ak_closure *row, ak_path *path, ak_owner *parent, ak_val item, ak_kind item_kind, int64_t i, ak_kind key_kind);
// ak_root_under makes a root owner (ak_root_new) whose parent is parent:
// it sees parent's contexts and catcher, and holds parent, not the effect
// running now.
ak_owner *ak_root_under(ak_owner *parent);
void ak_row_update(ak_row *rw, ak_val item, ak_kind item_kind, int64_t i);
void ak_row_free(ak_row *rw, ak_kind key_kind);

// ---- list.c ----

int64_t ak_list_count(ak_view *e);
void ak_list_bind(ak_view *e, int64_t index, ak_id cell);
void ak_list_unbind(ak_view *e, ak_id cell);
void ak_list_layout(ak_view *e, ak_lpass *p);
void ak_list_each_cell(ak_view *e, void (*f)(ak_view *c, void *ctx), void *ctx);

// ---- path.c ----

// ak_path_capture is the current path, retained, for a scope that builds
// later; ak_path_push makes the current path seg under base (count: a
// component's count added), ak_path_pop restores the one before.
ak_path *ak_path_capture(void);
void ak_path_release(ak_path *p);
ak_path *ak_path_push(ak_path *base, const char *seg, size_t n);
void ak_path_pop(ak_path *prev);
void ak_path_reset(void);

// ---- The reactive values of the VM (vm/react.c), which For's rows
// are made of ----

ak_val ak_vm_cell_new(ak_val v, ak_kind k, ak_str *keep_key);
ak_val ak_vm_cell_get(void *cell, bool track);
void ak_vm_cell_set(void *cell, ak_val v);  // takes v
ak_val ak_vm_acc_cell(void *cell_or_memo);
ak_val ak_vm_acc_get(void *acc);
void ak_vm_effect(ak_closure *c, bool render);

#endif
