#include "native/native.h"
#include "rt/sched.h"
#include "base/thread.h"

#include <stdio.h>

static const asuka_host_callbacks *host;

void ak_native_set_host(const asuka_host_callbacks *cb) { host = cb; }

// ---- Descriptors ----

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

// ---- Callbacks: functions the native side calls ----

typedef struct {
	uint64_t id;
	ak_closure *c;
	const char *desc;  // the F descriptor (kept by the module)
	const ak_json_types *types;
} callback;

static AK_VEC(callback) callbacks;
static uint64_t next_callback;

static uint64_t callback_add(ak_closure *c, const char *desc, const ak_json_types *t) {
	for (uint32_t i = 0; i < callbacks.len; i++)
		if (callbacks.data[i].c == c) return callbacks.data[i].id;
	callback cb = {++next_callback, ak_retain(c), desc, t};
	ak_vec_push(&callbacks, cb);
	return cb.id;
}

// ---- Encoding ----

bool ak_native_encode(ak_buf *b, ak_val v, const char *d, const ak_json_types *t) {
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
	case 'F':
		ak_buf_u64(b, v.p ? callback_add(v.p, d, t) : 0);
		return true;
	case '?':
		ak_buf_u8(b, v.p != NULL);
		if (!v.p) return true;
		if (kind_of(d + 1) != AK_K_REF) v = ((ak_box *)v.p)->v;
		return ak_native_encode(b, v, d + 1, t);
	case '[': {
		ak_arr *a = v.p;
		ak_buf_u32(b, a ? a->len : 0);
		for (uint32_t i = 0; a && i < a->len; i++)
			if (!ak_native_encode(b, a->data[i], d + 1, t)) return false;
		return true;
	}
	case '{': {
		ak_map *m = v.p;
		ak_buf_u32(b, m ? m->live : 0);
		for (uint32_t i = 0; m && i < m->len; i++) {
			if (m->dead[i]) continue;
			ak_str *k = m->keys[i].p;
			ak_buf_str(b, k->data, k->len);
			if (!ak_native_encode(b, m->vals[i], d + 1, t)) return false;
		}
		return true;
	}
	case 'S': {
		ak_struct *s = v.p;
		extern const char *ak_json_field_desc(const ak_shape *s, int i);
		for (uint16_t i = 0; s && i < s->shape->nfields; i++)
			if (!ak_native_encode(b, s->f[i], ak_json_field_desc(s->shape, i), t)) return false;
		return true;
	}
	}
	return false;
}

static void bad(const char *what) { ak_throw_msg("native: a bad answer (%s)", what); }

ak_val ak_native_decode(ak_reader *r, const char *d, const ak_json_types *t) {
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
		if (r->err || n > r->len) break;
		ak_kind k = kind_of(d + 1);
		ak_arr *a = ak_arr_new(k, NULL, n);
		if (d[1] == 's') a->h.flags = 1;
		for (uint32_t i = 0; i < n && !r->err && !ak_error_pending(); i++) a->data[a->len++] = ak_native_decode(r, d + 1, t);
		v.p = a;
		break;
	}
	case '{': {
		uint32_t n = ak_read_u32(r);
		if (r->err || n > r->len) break;
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
	if (r->err && !ak_error_pending()) bad("short");
	return v;
}

// ---- Objects ----

static void obj_drop(ak_opaque *o) {
	ak_native_obj *n = (ak_native_obj *)o;
	if (host && host->native_release && n->handle) host->native_release(n->handle);
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
		if (errors && i < errors->len) ak_throw(ak_retain(errors->data[i].p));
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

ak_val ak_native_new(const char *cls, size_t cls_len, uint32_t iface, ak_arr *errors) {
	if (!host || !host->native_new) {
		ak_throw_msg("unsupported: no native code here (%.*s)", (int)cls_len, cls);
		return (ak_val){0};
	}
	uint8_t out[256], *big = NULL;
	size_t n = host->native_new((const uint8_t *)cls, cls_len, iface, out, sizeof out, &big);
	ak_reader r = {big ? big : out, n, 0, false};
	ak_val v = {0};
	if (status(&r, errors)) {
		uint64_t h = ak_read_u64(&r);
		if (r.err) bad("handle");
		else v.p = ak_native_wrap(h);
	}
	if (big && host->native_free) host->native_free(big);
	return v;
}

static void encode_args(ak_buf *b, ak_val args, const char *d, const ak_json_types *t) {
	ak_struct *s = args.p;
	for (uint16_t i = 0; s && i < s->shape->nfields && *d; i++) {
		ak_native_encode(b, s->f[i], d, t);
		d = desc_skip(d);
	}
}

ak_val ak_native_call(ak_native_obj *o, uint32_t method, ak_val args, const char *args_desc, const char *ret_desc, ak_arr *errors, const ak_json_types *t) {
	if (!host || !host->native_call) {
		ak_throw_msg("unsupported: no native code here");
		return (ak_val){0};
	}
	ak_buf in = {0};
	encode_args(&in, args, args_desc, t);
	uint8_t out[512], *big = NULL;
	size_t n = host->native_call(o->handle, method, in.data, in.len, out, sizeof out, &big);
	ak_buf_free(&in);
	ak_reader r = {big ? big : out, n, 0, false};
	ak_val v = {0};
	if (status(&r, errors)) v = ak_native_decode(&r, ret_desc, t);
	if (big && host->native_free) host->native_free(big);
	return v;
}

// ---- Started calls ----

typedef struct {
	uint64_t call;
	ak_future *fut;
	const char *ret_desc;
	ak_arr *errors;
	const ak_json_types *types;
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
	encode_args(&in, args, args_desc, t);
	ak_mutex_lock(&calls_mu);
	uint64_t id = ++next_call;
	ak_vec_push(&calls, ((started){id, ak_retain(f), ret_desc, errors ? ak_retain(errors) : NULL, t}));
	ak_mutex_unlock(&calls_mu);
	f->ctx = (void *)(uintptr_t)id;
	f->cancel = start_cancel;
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

// ak_native_invoke: the native side calls a callback, on the main thread.
size_t ak_native_invoke(uint64_t id, const uint8_t *p, size_t len, uint8_t **out) {
	*out = NULL;
	callback *cb = NULL;
	for (uint32_t i = 0; i < callbacks.len; i++)
		if (callbacks.data[i].id == id) cb = &callbacks.data[i];
	if (!cb) return 0;
	const char *d = cb->desc;  // F<n><params><ret>
	int n = d[1] - '0';
	const char *pd = d + 2;
	ak_reader r = {p, len, 0, false};
	ak_val args[9];
	ak_kind kinds[9];
	int got = 0;
	for (int i = 0; i < n; i++) {
		kinds[i] = kind_of(pd);
		args[i] = ak_native_decode(&r, pd, cb->types);
		got++;
		pd = desc_skip(pd);
		if (ak_error_pending()) break;
	}
	size_t outlen = 0;
	if (!ak_error_pending()) {
		ak_closure *c = cb->c;
		ak_val res = ak_call(c, args, (uint32_t)n);
		if (!ak_error_pending() && *pd != 'v') {
			ak_buf b = {0};
			ak_native_encode(&b, res, pd, cb->types);
			*out = b.data;
			outlen = b.len;
		}
		ak_release_val(res, kind_of(pd));
	}
	for (int i = 0; i < got; i++) ak_release_val(args[i], kinds[i]);
	if (ak_error_pending()) {
		void *e = ak_error_take();
		fprintf(stderr, "asuka: a native callback failed: %s\n", ak_error_message(e)->data);
		ak_release(e);
	}
	return outlen;
}
