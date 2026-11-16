// native_view.c: a package's native component (docs/native.md, "Declaring
// a view"), shown as a Native view of its kind ("<class>#<Name>"), which
// each platform's generated code makes.
//
// Its props cross as Native.Prop: string name, u8 5 (bytes), u32 length,
// then the value in native calls' encoding (native/native.h), sent only
// when the encoded value changed. Its events come back as the Native
// event: string name, u32 count, then named values, the one named "v"
// the event's values in that encoding, decoded by the handler's
// descriptor and passed to it. The host measures it (sizeThatFits, its
// own measuring), again whenever a prop changes.
#include "ui/ui_int.h"
#include "native/native.h"

#include <stdio.h>

typedef struct {
	ak_str *name;  // retained
	ak_buf last;   // what was last sent
	bool sent;
} nprop;

typedef struct {
	ak_str *name;  // retained
	ak_closure *c; // retained
	ak_str *desc;  // retained: the values' descriptors, in order
	const ak_json_types *types;
} nhandler;

typedef struct {
	AK_VEC(nprop) props;
	AK_VEC(nhandler) handlers;
	ak_buf scratch;  // a prop's value, encoded (reused)
} native_state;

static bool str_is(const ak_str *s, const char *p, size_t n) { return s && s->len == n && memcmp(s->data, p, n) == 0; }

static void native_init(ak_view *e) {
	e->impl = ak_alloc(sizeof(native_state));
	ak_view_measure(e);
}

static void native_drop(ak_view *e) {
	native_state *s = e->impl;
	if (!s) return;
	for (uint32_t i = 0; i < s->props.len; i++) {
		ak_release(s->props.data[i].name);
		ak_buf_free(&s->props.data[i].last);
	}
	ak_vec_free(&s->props);
	for (uint32_t i = 0; i < s->handlers.len; i++) {
		ak_release(s->handlers.data[i].name);
		ak_release(s->handlers.data[i].c);
		ak_release(s->handlers.data[i].desc);
	}
	ak_vec_free(&s->handlers);
	ak_buf_free(&s->scratch);
	ak_free(s);
	e->impl = NULL;
}

static nprop *prop_of(native_state *s, const ak_str *name) {
	for (uint32_t i = 0; i < s->props.len; i++)
		if (str_is(s->props.data[i].name, name->data, name->len)) return &s->props.data[i];
	ak_vec_push(&s->props, ((nprop){.name = ak_retain((ak_str *)name)}));
	return &s->props.data[s->props.len - 1];
}

// send encodes v and sends it when it isn't what was last sent.
static void send(ak_view *e, const ak_str *name, ak_val v, const char *desc, const ak_json_types *t) {
	native_state *s = e->impl;
	if (e->dead || !s) return;
	ak_buf_reset(&s->scratch);
	if (!ak_native_encode(&s->scratch, v, desc, t)) return;
	nprop *p = prop_of(s, name);
	if (p->sent && p->last.len == s->scratch.len && memcmp(p->last.data, s->scratch.data, s->scratch.len) == 0) return;
	ak_buf_reset(&p->last);
	ak_buf_put(&p->last, s->scratch.data, s->scratch.len);
	p->sent = true;
	ak_buf *b = ak_batch_set_bytes_begin(e->r->batch, e->id, AK_PROP_NATIVE_PROP);
	ak_buf_str(b, name->data, name->len);
	ak_buf_u8(b, 5);
	ak_buf_str(b, (const char *)s->scratch.data, s->scratch.len);
	ak_batch_set_bytes_end(e->r->batch);
	ak_view_remeasure(e);
}

static void native_event(ak_view *e, const ak_event *ev) {
	native_state *s = e->impl;
	if (ev->kind != AK_EVENT_NATIVE || !s) return;
	ak_reader r = {(const uint8_t *)ev->s.p, ev->s.len, 0, false};
	uint32_t nn;
	const uint8_t *name = ak_read_bytes(&r, &nn);
	uint32_t count = ak_read_u32(&r);
	const uint8_t *val = NULL;
	uint32_t vn = 0;
	for (uint32_t i = 0; i < count && !r.err; i++) {
		uint32_t kn;
		const uint8_t *k = ak_read_bytes(&r, &kn);
		uint8_t type = ak_read_u8(&r);
		switch (type) {
		case 1: ak_read_u8(&r); break;
		case 2:
		case 3: ak_read_u64(&r); break;
		case 4:
		case 5: {
			uint32_t n;
			const uint8_t *p = ak_read_bytes(&r, &n);
			if (type == 5 && kn == 1 && k[0] == 'v') val = p, vn = n;
			break;
		}
		default: r.err = true;
		}
	}
	if (r.err) return;
	nhandler *h = NULL;
	for (uint32_t i = 0; i < s->handlers.len; i++)
		if (str_is(s->handlers.data[i].name, (const char *)name, nn)) h = &s->handlers.data[i];
	if (!h) return;
	nhandler hh = *h;  // the handler may be replaced while it runs
	ak_retain(hh.c);
	ak_retain(hh.desc);
	ak_reader vr = {val ? val : (const uint8_t *)"", vn, 0, false};
	ak_val args[9];
	ak_kind kinds[9];
	uint32_t n = 0;
	const char *d = hh.desc->data;
	while (*d && n < 9) {
		const char *next = ak_native_desc_skip(d);
		kinds[n] = ak_native_desc_kind(d);
		args[n++] = ak_native_decode(&vr, d, hh.types);
		d = next;
		if (ak_error_pending()) break;
	}
	if (ak_error_pending()) {
		void *err = ak_error_take();
		fprintf(stderr, "asuka: a native view's event %.*s is malformed: %s\n", (int)nn, name, ak_error_message(err)->data);
		ak_release(err);
	} else {
		ak_view_call_closure(e, hh.c, args, n);
	}
	for (uint32_t i = 0; i < n; i++) ak_release_val(args[i], kinds[i]);
	ak_release(hh.c);
	ak_release(hh.desc);
}

static const ak_el_class cls_native = {.name = "Native", .kind = AK_KIND_NATIVE, .flags = AK_ELF_MEASURED, .init = native_init, .event = native_event, .drop = native_drop};

ak_view *ak_ui_native(const ak_str *kind) {
	ak_ui_root *r = ak_ui_current();
	ak_view *e = ak_view_new(r, &cls_native, 0, true);
	ak_batch_set_string(r->batch, e->id, AK_PROP_NATIVE_TYPE, kind ? kind->data : "", kind ? kind->len : 0);
	ak_vec_push(&r->init_q, (ak_view *)ak_retain(e));
	return e;
}

static bool is_native(const ak_view *v) { return v && v->cls == &cls_native; }

void ak_ui_native_set(ak_view *v, const ak_str *name, ak_val value, const ak_str *desc, const ak_json_types *t) {
	if (!is_native(v)) ak_panic("ui.native_set: not a native view");
	send(v, name, value, desc->data, t);
}

// A binding: a prop that follows a closure, in a render effect.
typedef struct {
	ak_view *v;
	ak_closure *thunk;
	ak_str *name, *desc;
	const ak_json_types *types;
	ak_comp_info info;
} nbinding;

static void nbinding_run(void *ctx) {
	nbinding *b = ctx;
	ak_val v = ak_call(b->thunk, NULL, 0);
	if (ak_error_pending()) {
		void *err = ak_error_take();
		if (ak_is_cancel(err)) ak_release(err);
		else ak_raise(err);
		return;
	}
	send(b->v, b->name, v, b->desc->data, b->types);
	ak_release_val(v, ak_native_desc_kind(b->desc->data));
}

static void nbinding_drop(void *ctx) {
	nbinding *b = ctx;
	ak_release(b->v);
	ak_release(b->thunk);
	ak_release(b->name);
	ak_release(b->desc);
	ak_free(b);
}

void ak_ui_native_bind(ak_view *v, const ak_str *name, ak_closure *thunk, const ak_str *desc, const ak_json_types *t) {
	if (!is_native(v)) ak_panic("ui.native_bind: not a native view");
	nbinding *b = ak_alloc(sizeof *b);
	b->v = ak_retain(v);
	b->thunk = ak_retain(thunk);
	b->name = ak_retain((ak_str *)name);
	b->desc = ak_retain((ak_str *)desc);
	b->types = t;
	b->info = (ak_comp_info){.drop = nbinding_drop, .label = "native prop"};
	ak_effect_new(nbinding_run, b, AK_EFFECT_RENDER, &b->info);
}

void ak_ui_native_on(ak_view *v, const ak_str *name, ak_closure *handler, const ak_str *desc, const ak_json_types *t) {
	if (!is_native(v)) ak_panic("ui.native_on: not a native view");
	native_state *s = v->impl;
	if (!handler || !s) return;
	for (uint32_t i = 0; i < s->handlers.len; i++) {
		nhandler *h = &s->handlers.data[i];
		if (!str_is(h->name, name->data, name->len)) continue;
		ak_retain(handler);
		ak_release(h->c);
		h->c = handler;
		return;
	}
	ak_vec_push(&s->handlers, ((nhandler){ak_retain((ak_str *)name), ak_retain(handler), ak_retain((ak_str *)desc), t}));
	ak_view_listen(v, AK_EVENT_NATIVE);
}
