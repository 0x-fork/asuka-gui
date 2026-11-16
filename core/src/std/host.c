// asuka/host's runtime (std/host: its native fns are these builtins): the
// little-endian values the hosts' own modules take and answer, as bytes.
//
//   host.u8(v) host.u32(v) host.u64(v) host.f64(v) -> bytes
//   host.str(s) -> bytes                           a u32 length, then the UTF-8
//   host.readU8(b, at) host.readU32(b, at) host.readU64(b, at) -> int
//   host.readF64(b, at) -> float
//   host.readStr(b, at) -> string                  at a u32 length
//
// A read past the end gives 0 (or "").
#include "vm/vm_int.h"

static ak_val le(uint64_t v, int n) {
	uint8_t b[8];
	for (int i = 0; i < n; i++) b[i] = (uint8_t)(v >> (8 * i));
	return (ak_val){.p = ak_bytes_new(b, (size_t)n)};
}

static ak_val b_u8(ak_val *a, uint32_t n) { AK_UNUSED(n); return le((uint64_t)a[0].i, 1); }
static ak_val b_u32(ak_val *a, uint32_t n) { AK_UNUSED(n); return le((uint64_t)a[0].i, 4); }
static ak_val b_u64(ak_val *a, uint32_t n) { AK_UNUSED(n); return le((uint64_t)a[0].i, 8); }

static ak_val b_f64(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	uint64_t u;
	memcpy(&u, &a[0].f, 8);
	return le(u, 8);
}

static ak_val b_str(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_str *s = a[0].p;
	ak_bytes *b = ak_bytes_new(NULL, 4 + (size_t)s->len);
	for (int i = 0; i < 4; i++) b->data[i] = (uint8_t)(s->len >> (8 * i));
	memcpy(b->data + 4, s->data, s->len);
	return (ak_val){.p = b};
}

static uint64_t rd(ak_val *a, int n, bool *ok) {
	ak_bytes *b = a[0].p;
	int64_t at = a[1].i;
	*ok = at >= 0 && (uint64_t)at + (uint64_t)n <= b->len;
	if (!*ok) return 0;
	uint64_t v = 0;
	for (int i = 0; i < n; i++) v |= (uint64_t)b->data[at + i] << (8 * i);
	return v;
}

static ak_val b_read_u8(ak_val *a, uint32_t n) { AK_UNUSED(n); bool ok; return (ak_val){.i = (int64_t)rd(a, 1, &ok)}; }
static ak_val b_read_u32(ak_val *a, uint32_t n) { AK_UNUSED(n); bool ok; return (ak_val){.i = (int64_t)rd(a, 4, &ok)}; }
static ak_val b_read_u64(ak_val *a, uint32_t n) { AK_UNUSED(n); bool ok; return (ak_val){.i = (int64_t)rd(a, 8, &ok)}; }

static ak_val b_read_f64(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	bool ok;
	uint64_t u = rd(a, 8, &ok);
	double f;
	memcpy(&f, &u, 8);
	return (ak_val){.f = f};
}

static ak_val b_read_str(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	bool ok;
	uint64_t len = rd(a, 4, &ok);
	ak_bytes *b = a[0].p;
	uint64_t at = (uint64_t)a[1].i + 4;
	if (!ok || at + len > b->len) return (ak_val){.p = ak_str_empty()};
	return (ak_val){.p = ak_str_new((const char *)b->data + at, (size_t)len)};
}

void ak_std_host_init(void) {
	ak_builtin_register("host.u8", b_u8);
	ak_builtin_register("host.u32", b_u32);
	ak_builtin_register("host.u64", b_u64);
	ak_builtin_register("host.f64", b_f64);
	ak_builtin_register("host.str", b_str);
	ak_builtin_register("host.readU8", b_read_u8);
	ak_builtin_register("host.readU32", b_read_u32);
	ak_builtin_register("host.readU64", b_read_u64);
	ak_builtin_register("host.readF64", b_read_f64);
	ak_builtin_register("host.readStr", b_read_str);
}
