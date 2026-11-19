#include "native/native.h"
#include "rt/sched.h"
#include "base/thread.h"
#include "ui/ui.h"

#include <stdio.h>

static const asuka_host_callbacks *host;

void ak_native_set_host(const asuka_host_callbacks *cb) { host = cb; }

// ---- Descriptors ----

static const char *desc_skip(const char *d);

const char *ak_native_desc_skip(const char *d) { return desc_skip(d); }

static const char *desc_skip(const char *d) {
	switch (*d) {
	case '?':
	case '[':
	case '{':
		return desc_skip(d + 1);
	case 'S':
	case 'E':
	case 'e': {
		const char *q = strchr(d, ';');
		return q ? q + 1 : d + strlen(d);
	}
	case 'F': {
		int n = d[1] - '0';
		const char *p = d + 2;
		for (int i = 0; i < n; i++) p = desc_skip(p);
		return desc_skip(p);
	}
	default:
		return *d ? d + 1 : d;
	}
}

static ak_kind kind_of(const char *d) {
	switch (*d) {
	case 'i':
	case 'e':
		return AK_K_INT;
	case 'f':
		return AK_K_FLOAT;
	case 'b':
		return AK_K_BOOL;
	case 'v':
		return AK_K_VOID;
	default:
		return AK_K_REF;
	}
}

static size_t name_len(const char *d) {
	const char *q = strchr(d + 1, ';');
	return q ? (size_t)(q - d - 1) : strlen(d + 1);
}

ak_kind ak_native_desc_kind(const char *d) { return kind_of(d); }

// ---- Callbacks: functions the native side calls ----
//
// A callback lives as long as the object it was passed to (docs/
// native.md): its entry, holding the closure and the module its
// descriptor and types are the module's, goes when the object does
// (obj_drop). A closure passed to an object twice with the same
// descriptor is one callback; with another descriptor, another.

typedef struct {
	uint64_t id;
	ak_closure *c;                // retained
	const char *desc;             // the F descriptor (held through types)
	const ak_json_types *types;   // held (ak_json_types.hold)
	const ak_native_obj *owner;   // the object it was passed to
} callback;

static AK_VEC(callback) callbacks;
static uint64_t next_callback;

static void types_hold(const ak_json_types *t, bool keep) {
	if (t && t->hold) t->hold(t->ctx, keep);
}

static size_t desc_len(const char *d) { return (size_t)(desc_skip(d) - d); }

static uint64_t callback_add(ak_closure *c, const char *desc, const ak_json_types *t, const ak_native_obj *owner) {
	size_t n = desc_len(desc);
	for (uint32_t i = 0; i < callbacks.len; i++) {
		const callback *cb = &callbacks.data[i];
		if (cb->c == c && cb->owner == owner && cb->types == t && desc_len(cb->desc) == n && memcmp(cb->desc, desc, n) == 0) return cb->id;
	}
	types_hold(t, true);
	callback cb = {++next_callback, ak_retain(c), desc, t, owner};
	ak_vec_push(&callbacks, cb);
	return cb.id;
}

// callbacks_drop lets the callbacks passed to owner go, as it goes.
static void callbacks_drop(const ak_native_obj *owner) {
	// Taken out of the table first: letting a closure go may let other
	// objects go, whose callbacks go from the table under this loop.
	AK_VEC(callback) gone = {0};
	for (uint32_t i = 0; i < callbacks.len;) {
		if (callbacks.data[i].owner != owner) {
			i++;
			continue;
		}
		ak_vec_push(&gone, callbacks.data[i]);
		callbacks.data[i] = callbacks.data[--callbacks.len];
	}
	for (uint32_t i = 0; i < gone.len; i++) {
		ak_release(gone.data[i].c);
		types_hold(gone.data[i].types, false);
	}
	ak_vec_free(&gone);
}

// ---- Encoding ----

// encode writes v; a function in it becomes a callback of owner, the
// object the call that encodes it is to (none: a view's prop, which the
// checker keeps functions out of, crosses as none).
static bool encode(ak_buf *b, ak_val v, const char *d, const ak_json_types *t, const ak_native_obj *owner);

bool ak_native_encode(ak_buf *b, ak_val v, const char *d, const ak_json_types *t) { return encode(b, v, d, t, NULL); }

static bool encode(ak_buf *b, ak_val v, const char *d, const ak_json_types *t, const ak_native_obj *owner) {
	switch (*d) {
	case 'i':
	case 'e':
		ak_buf_u64(b, (uint64_t)v.i);
		return true;
	case 'f':
		ak_buf_f64(b, v.f);
		return true;
	case 'b':
		ak_buf_u8(b, v.i != 0);
		return true;
	case 'v':
		return true;
	case 's': {
		ak_str *s = v.p;
		ak_buf_str(b, s ? s->data : "", s ? s->len : 0);
		return true;
	}
	case 'y': {
		ak_bytes *s = v.p;
		ak_buf_str(b, s ? (const char *)s->data : "", s ? s->len : 0);
		return true;
	}
	case 'o': {
		ak_native_obj *o = v.p;
		ak_buf_u64(b, o ? o->handle : 0);
		return true;
	}
	case 'V':
		// A view (a Ref's, read by the call): the id its host knows it
		// by, 0 for none or one that isn't shown.
		ak_buf_u64(b, v.p && ((ak_obj *)v.p)->tag == AK_TAG_VIEW ? ak_ui_view_id(v.p) : 0);
		return true;
	case 'F':
		ak_buf_u64(b, v.p && owner ? callback_add(v.p, d, t, owner) : 0);
		return true;
	case '?':
		ak_buf_u8(b, v.p != NULL);
		if (!v.p) return true;
		if (kind_of(d + 1) != AK_K_REF) v = ((ak_box *)v.p)->v;
		return encode(b, v, d + 1, t, owner);
	case '[': {
		ak_arr *a = v.p;
		ak_buf_u32(b, a ? a->len : 0);
		for (uint32_t i = 0; a && i < a->len; i++)
			if (!encode(b, ak_arr_get(a, i), d + 1, t, owner)) return false;
		return true;
	}
	case '{': {
		ak_map *m = v.p;
		ak_buf_u32(b, m ? m->live : 0);
		for (uint32_t i = 0; m && i < m->len; i++) {
			if (m->dead[i]) continue;
			ak_str *k = m->keys[i].p;
			ak_buf_str(b, k->data, k->len);
			if (!encode(b, m->vals[i], d + 1, t, owner)) return false;
		}
		return true;
	}
	case 'S': {
		ak_struct *s = v.p;
		extern const char *ak_json_field_desc(const ak_shape *s, int i);
		for (uint16_t i = 0; s && i < s->shape->nfields; i++)
			if (!encode(b, s->f[i], ak_json_field_desc(s->shape, i), t, owner)) return false;
		return true;
	}
	}
	return false;
}

static void bad(const char *what) { ak_throw_msg("native: a bad answer (%s)", what); }

static ak_val decode(ak_reader *r, const char *d, const ak_json_types *t);

// ak_native_decode reads a value; an answer cut short or wrong throws and
// returns the zero value, having released what it had built.
ak_val ak_native_decode(ak_reader *r, const char *d, const ak_json_types *t) {
	ak_val v = decode(r, d, t);
	if (r->err && !ak_error_pending()) bad("short");
	if (!ak_error_pending()) return v;
	ak_release_val(v, kind_of(d));
	return (ak_val){0};
}

static ak_val decode(ak_reader *r, const char *d, const ak_json_types *t) {
	ak_val v = {0};
	switch (*d) {
	case 'i':
	case 'e':
		v.i = (int64_t)ak_read_u64(r);
		break;
	case 'f':
		v.f = ak_read_f64(r);
		break;
	case 'b':
		v.i = ak_read_u8(r) != 0;
		break;
	case 'v':
		break;
	case 's':
	case 'y': {
		uint32_t n;
		const uint8_t *p = ak_read_bytes(r, &n);
		if (r->err) break;
		v.p = *d == 's' ? (void *)ak_str_new((const char *)p, n) : (void *)ak_bytes_new(p, n);
		break;
	}
	case 'o': {
		uint64_t h = ak_read_u64(r);
		if (h) v.p = ak_native_wrap(h);
		break;
	}
	case '?':
		if (ak_read_u8(r)) {
			ak_kind k = kind_of(d + 1);
			v = ak_native_decode(r, d + 1, t);
			if (!ak_error_pending() && k != AK_K_REF) v.p = ak_box_new(k, v);
		}
		break;
	case '[': {
		uint32_t n = ak_read_u32(r);
		if (n > r->len - r->off) r->err = true;  // more items than bytes left
		if (r->err) break;
		ak_kind k = kind_of(d + 1);
		ak_arr *a = ak_arr_new(k, NULL, n);
		if (d[1] == 's') a->h.flags = 1;
		for (uint32_t i = 0; i < n && !r->err && !ak_error_pending(); i++) ak_arr_put(a, a->len++, ak_native_decode(r, d + 1, t));
		v.p = a;
		break;
	}
	case '{': {
		uint32_t n = ak_read_u32(r);
		if (n > r->len - r->off) r->err = true;
		if (r->err) break;
		ak_map *m = ak_map_new(AK_K_REF, true, kind_of(d + 1), NULL);
		for (uint32_t i = 0; i < n && !r->err && !ak_error_pending(); i++) {
			uint32_t kn;
			const uint8_t *kp = ak_read_bytes(r, &kn);
			if (r->err) break;
			ak_val key = {.p = ak_str_new((const char *)kp, kn)};
			ak_map_set(&m, key, ak_native_decode(r, d + 1, t));
		}
		v.p = m;
		break;
	}
	case 'S': {
		const ak_shape *s = t && t->shape ? t->shape(t->ctx, d + 1, name_len(d)) : NULL;
		if (!s) {
			ak_throw_msg("native: no type %.*s", (int)name_len(d), d + 1);
			return v;
		}
		ak_struct *o = ak_struct_new(s);
		extern const char *ak_json_field_desc(const ak_shape *s, int i);
		for (uint16_t i = 0; i < s->nfields && !r->err && !ak_error_pending(); i++) o->f[i] = ak_native_decode(r, ak_json_field_desc(s, i), t);
		v.p = o;
		break;
	}
	default:
		bad(d);
		return v;
	}
	return v;
}

// ---- Objects ----

static void obj_drop(ak_opaque *o) {
	ak_native_obj *n = (ak_native_obj *)o;
	if (host && host->native_release && n->handle) host->native_release(n->handle);
	callbacks_drop(n);
}

ak_native_obj *ak_native_wrap(uint64_t h) {
	ak_native_obj *o = ak_opaque_new(AK_TAG_NATIVE, sizeof(ak_native_obj), obj_drop);
	o->handle = h;
	return o;
}

// status reads an answer's status: true for ok (r at the result), else
// it throws the error.
static bool status(ak_reader *r, ak_arr *errors) {
	uint8_t s = ak_read_u8(r);
	switch (s) {
	case 0:
		if (r->err) {
			bad("empty");
			return false;
		}
		return true;
	case 1: {
		uint32_t i = ak_read_u32(r);
		if (errors && i < errors->len) ak_throw(ak_retain(ak_arr_vals(errors)[i].p));
		else ak_throw_msg("native: an error (%u) the package doesn't declare", i);
		return false;
	}
	case 2: {
		uint32_t dn, mn;
		const uint8_t *dp = ak_read_bytes(r, &dn);
		int64_t code = (int64_t)ak_read_u64(r);
		const uint8_t *mp = ak_read_bytes(r, &mn);
		if (dn) ak_throw_msg("%.*s (%.*s %lld)", (int)mn, mp ? (const char *)mp : "", (int)dn, (const char *)dp, (long long)code);
		else ak_throw_msg("%.*s", (int)mn, mp ? (const char *)mp : "");
		return false;
	}
	case 3: {
		uint32_t mn;
		const uint8_t *mp = ak_read_bytes(r, &mn);
		ak_throw_msg("unsupported: %.*s", (int)mn, mp ? (const char *)mp : "");
		return false;
	}
	case 4:
		ak_throw_cancel();
		return false;
	}
	bad("status");
	return false;
}

// answer_len is how much of an answer to read: a host's answer longer
// than out that isn't in memory of its own (*big) breaks the contract,
// and is read as none (a bad answer) rather than past out.
static size_t answer_len(size_t n, size_t cap, const uint8_t *big) { return big || n <= cap ? n : 0; }

ak_val ak_native_new(const char *cls, size_t cls_len, uint32_t iface, ak_arr *errors) {
	if (!host || !host->native_new) {
		ak_throw_msg("unsupported: no native code here (%.*s)", (int)cls_len, cls);
		return (ak_val){0};
	}
	uint8_t out[256], *big = NULL;
	size_t n = host->native_new((const uint8_t *)cls, cls_len, iface, out, sizeof out, &big);
	ak_reader r = {big ? big : out, answer_len(n, sizeof out, big), 0, false};
	ak_val v = {0};
	if (status(&r, errors)) {
		uint64_t h = ak_read_u64(&r);
		if (r.err) bad("handle");
		else v.p = ak_native_wrap(h);
	}
	if (big && host->native_free) host->native_free(big);
	return v;
}

static void encode_args(ak_buf *b, const ak_native_obj *o, ak_val args, const char *d, const ak_json_types *t) {
	ak_struct *s = args.p;
	for (uint16_t i = 0; s && i < s->shape->nfields && *d; i++) {
		encode(b, s->f[i], d, t, o);
		d = desc_skip(d);
	}
}

ak_val ak_native_call(ak_native_obj *o, uint32_t method, ak_val args, const char *args_desc, const char *ret_desc, ak_arr *errors, const ak_json_types *t) {
	if (!host || !host->native_call) {
		ak_throw_msg("unsupported: no native code here");
		return (ak_val){0};
	}
	ak_buf in = {0};
	encode_args(&in, o, args, args_desc, t);
	uint8_t out[512], *big = NULL;
	size_t n = host->native_call(o->handle, method, in.data, in.len, out, sizeof out, &big);
	ak_buf_free(&in);
	ak_reader r = {big ? big : out, answer_len(n, sizeof out, big), 0, false};
	ak_val v = {0};
	if (status(&r, errors)) v = ak_native_decode(&r, ret_desc, t);
	if (big && host->native_free) host->native_free(big);
	return v;
}

// ---- Started calls ----

// A started call holds what its answer is read by until it answers:
// its errors, and the module its result's descriptor and types are the
// module's (which a dev reload may otherwise free first); and its
// object, with the callbacks passed to it, which the call may still call.
typedef struct {
	uint64_t call;
	ak_native_obj *obj;  // retained
	ak_future *fut;
	const char *ret_desc;
	ak_arr *errors;
	const ak_json_types *types;  // held
} started;

static AK_VEC(started) calls;
static uint64_t next_call;
static ak_mutex calls_mu = AK_MUTEX_INIT;

static void start_cancel(ak_future *f) {
	uint64_t id = (uint64_t)(uintptr_t)f->ctx;
	if (host && host->native_cancel) host->native_cancel(id);
	// The call still answers (cancelled, or what it had done).
}

ak_future *ak_native_start(ak_native_obj *o, uint32_t method, ak_val args, const char *args_desc, const char *ret_desc, ak_arr *errors, const ak_json_types *t) {
	ak_future *f = ak_future_new(kind_of(ret_desc));
	if (!host || !host->native_start) {
		ak_throw_msg("unsupported: no native code here");
		ak_future_reject(f, ak_error_take());
		return f;
	}
	ak_buf in = {0};
	encode_args(&in, o, args, args_desc, t);
	types_hold(t, true);
	ak_mutex_lock(&calls_mu);
	uint64_t id = ++next_call;
	ak_vec_push(&calls, ((started){id, ak_retain(o), ak_retain(f), ret_desc, errors ? ak_retain(errors) : NULL, t}));
	ak_mutex_unlock(&calls_mu);
	f->ctx = (void *)(uintptr_t)id;
	f->cancel = start_cancel;
	f->held = 1;  // calls' reference, until it answers
	ak_sched_hold();  // until it answers
	host->native_start(o->handle, method, in.data, in.len, id);
	ak_buf_free(&in);
	return f;
}

typedef struct {
	started s;
	uint8_t *p;
	size_t len;
} answer;

static void deliver(void *ctx) {
	answer *a = ctx;
	ak_reader r = {a->p, a->len, 0, false};
	if (status(&r, a->s.errors)) {
		ak_val v = ak_native_decode(&r, a->s.ret_desc, a->s.types);
		if (ak_error_pending()) ak_future_reject(a->s.fut, ak_error_take());
		else ak_future_resolve(a->s.fut, v);
	} else {
		ak_future_reject(a->s.fut, ak_error_take());
	}
	ak_release(a->s.fut);
	ak_release(a->s.errors);
	ak_release(a->s.obj);
	types_hold(a->s.types, false);  // after the future, which may hold the result, went
	ak_free(a->p);
	ak_free(a);
	ak_sched_unhold();
}

// ak_native_done may come from any thread: the answer is copied and
// delivered on the main thread.
void ak_native_done(uint64_t call, const uint8_t *p, size_t len) {
	ak_mutex_lock(&calls_mu);
	answer *a = NULL;
	for (uint32_t i = 0; i < calls.len; i++) {
		if (calls.data[i].call != call) continue;
		a = ak_alloc(sizeof *a);
		a->s = calls.data[i];
		calls.data[i] = calls.data[--calls.len];
		break;
	}
	ak_mutex_unlock(&calls_mu);
	if (!a) return;
	a->p = ak_alloc(len ? len : 1);
	if (len) memcpy(a->p, p, len);
	a->len = len;
	ak_post(deliver, a);
}

// after_invoke runs nothing: posted after a callback, it has the host run
// what the callback made ready (a stream's reader, a task's await) and
// flush what it wrote, as an event would, which a call from the host's
// own code (a notification) doesn't.
static void after_invoke(void *ctx) { AK_UNUSED(ctx); }

// ak_native_invoke: the native side calls a callback, on the main thread.
size_t ak_native_invoke(uint64_t id, const uint8_t *p, size_t len, uint8_t **out) {
	*out = NULL;
	// A copy: the callback's body may register callbacks of its own,
	// growing the table under it.
	callback cb = {0};
	for (uint32_t i = 0; i < callbacks.len; i++)
		if (callbacks.data[i].id == id) cb = callbacks.data[i];
	if (!cb.c) return 0;
	// Held for the call: its body may let its object, and so the
	// callback, go.
	ak_retain(cb.c);
	types_hold(cb.types, true);
	const char *d = cb.desc;  // F<n><params><ret>
	int n = d[1] - '0';
	const char *pd = d + 2;
	ak_reader r = {p, len, 0, false};
	ak_val args[9];
	ak_kind kinds[9];
	int got = 0;
	for (int i = 0; i < n; i++) {
		kinds[i] = kind_of(pd);
		args[i] = ak_native_decode(&r, pd, cb.types);
		got++;
		pd = desc_skip(pd);
		if (ak_error_pending()) break;
	}
	size_t outlen = 0;
	if (!ak_error_pending()) {
		ak_val res = ak_call(cb.c, args, (uint32_t)n);
		if (!ak_error_pending() && *pd != 'v') {
			ak_buf b = {0};
			ak_native_encode(&b, res, pd, cb.types);
			*out = b.data;
			outlen = b.len;
		}
		ak_release_val(res, kind_of(pd));
	}
	for (int i = 0; i < got; i++) ak_release_val(args[i], kinds[i]);
	ak_release(cb.c);
	types_hold(cb.types, false);
	if (ak_error_pending()) {
		void *e = ak_error_take();
		fprintf(stderr, "asuka: a native callback failed: %s\n", ak_error_message(e)->data);
		ak_release(e);
	}
	ak_post(after_invoke, NULL);
	return outlen;
}
