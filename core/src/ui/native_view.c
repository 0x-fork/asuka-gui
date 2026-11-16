// native_view.c: a native package's view (a `native component`,
// docs/native.md), as ops/schema.go's Native kind: its Type ("<class>#
// <name>": the package's class and the component's name) sent before its
// props, each prop one Prop op (a u32-length name, u8 5, the value's
// bytes on the native wire, a u32 length first), sent as it changes; the
// host's Native events (a string name, u32 count, then named values: the
// event's arguments are the one named "v", on the native wire) call the
// handlers by their name. The host measures it.
//
// The values' encoding is the VM's (vm/native_lib.c's ui.native_*
// builtins): the view gets bytes, and the handlers' descriptors and the
// module's types to decode with.
#include "ui/ui_int.h"
#include "native/native.h"

#include <stdio.h>

typedef struct {
	ak_str *name;
	ak_buf bytes;  // the last sent (or to send at finalize)
} native_prop;

typedef struct {
	ak_str *name;
	ak_str *desc;  // the parameters' descriptors, in order
	const ak_json_types *types;
	uint16_t ev;  // its handler's entry in the view's handlers
} native_event;

typedef struct {
	ak_str *type;
	AK_VEC(native_prop) props;
	AK_VEC(native_event) events;
} native_state;

// Handlers of a native view are kept with the view's, as events past the
// prelude's.
enum { NATIVE_EV_BASE = 0x8000 };

static void native_init(ak_view *e) {
	ak_view_measure(e);
	e->impl = ak_alloc(sizeof(native_state));
}

static void native_drop(ak_view *e) {
	native_state *s = e->impl;
	if (!s) return;
	if (s->type) ak_release(s->type);
	for (uint32_t i = 0; i < s->props.len; i++) {
		ak_release(s->props.data[i].name);
		ak_buf_free(&s->props.data[i].bytes);
	}
	for (uint32_t i = 0; i < s->events.len; i++) {
		ak_release(s->events.data[i].name);
		ak_release(s->events.data[i].desc);
	}
	ak_vec_free(&s->props);
	ak_vec_free(&s->events);
	ak_free(s);
	e->impl = NULL;
}

static void send_prop(ak_view *e, const native_prop *p) {
	ak_buf *b = ak_batch_set_bytes_begin(ak_view_batch(e), e->id, AK_PROP_NATIVE_PROP);
	ak_buf_str(b, p->name->data, p->name->len);
	ak_buf_u8(b, 5);
	ak_buf_str(b, (const char *)p->bytes.data, p->bytes.len);
	ak_batch_set_bytes_end(ak_view_batch(e));
}

static void native_finalize(ak_view *e) {
	native_state *s = e->impl;
	if (s->type) ak_batch_set_string(ak_view_batch(e), e->id, AK_PROP_NATIVE_TYPE, s->type->data, s->type->len);
	for (uint32_t i = 0; i < s->props.len; i++) send_prop(e, &s->props.data[i]);
	if (s->events.len) ak_view_listen(e, AK_EVENT_NATIVE);
}

static void native_event_in(ak_view *e, const ak_event *ev) {
	if (ev->kind != AK_EVENT_NATIVE) return;
	native_state *s = e->impl;
	ak_reader r = {(const uint8_t *)ev->s.p, ev->s.len, 0, false};
	ak_span name = ak_read_str(&r);
	native_event *h = NULL;
	for (uint32_t i = 0; i < s->events.len; i++)
		if (ak_span_eq(ak_strn(s->events.data[i].name->data, s->events.data[i].name->len), name)) h = &s->events.data[i];
	if (!h) return;
	// The arguments: the value named "v" (none: the event has none).
	ak_span args = {0};
	uint32_t n = ak_read_u32(&r);
	for (uint32_t i = 0; i < n && !r.err; i++) {
		ak_span k = ak_read_str(&r);
		uint8_t t = ak_read_u8(&r);
		ak_span v = {0};
		switch (t) {
		case 1: ak_read_u8(&r); break;
		case 2:
		case 3: ak_read_u64(&r); break;
		case 4:
		case 5: v = ak_read_str(&r); break;
		default: return;
		}
		if (t == 5 && ak_span_eqc(k, "v")) args = v;
	}
	if (r.err) return;
	// Each parameter's value, by its descriptor.
	ak_val vals[16];
	ak_kind kinds[16];
	uint32_t nv = 0;
	ak_reader ar = {(const uint8_t *)args.p, args.len, 0, false};
	for (const char *d = h->desc->data; *d && nv < AK_LEN(vals); d = ak_native_desc_skip(d)) {
		kinds[nv] = ak_native_desc_kind(d);
		vals[nv] = ak_native_decode(&ar, d, h->types);
		if (ak_error_pending()) {
			void *err = ak_error_take();
			fprintf(stderr, "asuka: a native view's event %.*s: %s\n", (int)name.len, name.p, ak_error_message(err)->data);
			ak_release(err);
			for (uint32_t i = 0; i < nv; i++) ak_release_val(vals[i], kinds[i]);
			return;
		}
		nv++;
	}
	ak_view_call(e, h->ev, vals, nv);
	for (uint32_t i = 0; i < nv; i++) ak_release_val(vals[i], kinds[i]);
}

static const ak_el_class cls_native = {.name = "Native", .kind = AK_KIND_NATIVE, .flags = AK_ELF_MEASURED, .init = native_init,
                                       .finalize = native_finalize, .event = native_event_in, .drop = native_drop};

ak_view *ak_ui_native(ak_str *type) {
	ak_ui_root *r = ak_ui_current();
	ak_view *e = ak_view_new(r, &cls_native, 0, true);
	native_state *s = e->impl;
	s->type = ak_retain(type);
	ak_vec_push(&r->init_q, (ak_view *)ak_retain(e));
	return e;
}

void ak_ui_native_set(ak_view *e, ak_str *name, const uint8_t *bytes, size_t n) {
	if (e->cls != &cls_native) ak_panic("ui: %s isn't a native view", e->cls->name);
	if (e->dead) return;
	native_state *s = e->impl;
	native_prop *p = NULL;
	for (uint32_t i = 0; i < s->props.len; i++)
		if (s->props.data[i].name->len == name->len && memcmp(s->props.data[i].name->data, name->data, name->len) == 0) p = &s->props.data[i];
	if (p) {
		if (p->bytes.len == n && (n == 0 || memcmp(p->bytes.data, bytes, n) == 0)) return;
		p->bytes.len = 0;
	} else {
		ak_vec_push(&s->props, ((native_prop){.name = ak_retain(name)}));
		p = &s->props.data[s->props.len - 1];
	}
	ak_buf_put(&p->bytes, bytes, n);
	if (e->finalized) send_prop(e, p);
}

void ak_ui_native_on(ak_view *e, ak_str *name, ak_str *desc, const void *types, ak_closure *handler) {
	if (e->cls != &cls_native) ak_panic("ui: %s isn't a native view", e->cls->name);
	native_state *s = e->impl;
	if (!handler) return;
	uint16_t ev = (uint16_t)(NATIVE_EV_BASE + s->events.len);
	ak_vec_push(&s->events, ((native_event){.name = ak_retain(name), .desc = ak_retain(desc), .types = types, .ev = ev}));
	ak_vec_push(&e->handlers, ((ak_handler_ent){ev, ak_retain(handler)}));
	if (e->finalized) ak_view_listen(e, AK_EVENT_NATIVE);
}
