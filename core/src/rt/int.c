// int.c: the sized integers' builtins (i8 to u64) that aren't the
// int's with a type's code more (lib.c's int.to_string, int.padded and
// their kin take one): a type's code is rt.h's ak_int_type.
#include "rt/lib.h"

#define A(i) (args[i])
#define STR(i) ((ak_str *)args[i].p)

// int.abs(v, t): v without its sign at signed type t; its smallest has
// no positive, which throws as -x does.
static ak_val int_abs(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t v = A(0).i;
	if (v == ak_int_min((int)A(1).i)) {
		ak_throw_msg("integer overflow");
		return (ak_val){0};
	}
	return (ak_val){.i = v < 0 ? -v : v};
}

// int.exactly(v, t): v, of type t >> 4, as type t & 15 (a box), or none
// when it doesn't fit: u8(exactly: n).
static ak_val int_exactly(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int t = (int)A(1).i;
	if (!ak_int_fits(A(0).i, t >> 4, t & 15)) return (ak_val){.p = NULL};
	return (ak_val){.p = ak_box_new(AK_K_INT, A(0))};
}

// int.exactly_float(x, t): x as type t when it's a whole number in its
// range (a box), else none: int(exactly: 2.0) is 2, of 2.5 none.
static ak_val int_exactly_float(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	double d = A(0).f;
	int t = (int)A(1).i;
	if (d != d || d != trunc(d)) return (ak_val){.p = NULL};
	int64_t v;
	if (t == AK_U64) {
		if (d < 0 || d >= 18446744073709551616.0) return (ak_val){.p = NULL};
		v = (int64_t)(uint64_t)d;
	} else {
		if (d < -9223372036854775808.0 || d >= 9223372036854775808.0) return (ak_val){.p = NULL};
		v = (int64_t)d;
		if (!ak_int_fits(v, AK_I64, t)) return (ak_val){.p = NULL};
	}
	return (ak_val){.p = ak_box_new(AK_K_INT, (ak_val){.i = v})};
}

// int.parse(text, radix, t): the whole text as an integer of type t in
// radix (2 to 36, letters in either case), an optional sign first; none
// when it isn't one or doesn't fit. A radix outside 2 to 36 throws.
static ak_val int_parse(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *s = STR(0);
	int64_t radix = A(1).i;
	if (radix < 2 || radix > 36) {
		ak_throw_msg("parse's radix is %lld: it must be 2 to 36", (long long)radix);
		return (ak_val){0};
	}
	int64_t v;
	if (!s || !ak_int_parse(s->data, s->len, (int)radix, (int)A(2).i, &v)) return (ak_val){.p = NULL};
	return (ak_val){.p = ak_box_new(AK_K_INT, (ak_val){.i = v})};
}

// math.minu and math.maxu: min(a, b) and max(a, b) of u64s.
static ak_val m_minu(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return (uint64_t)A(0).i < (uint64_t)A(1).i ? A(0) : A(1);
}

static ak_val m_maxu(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return (uint64_t)A(0).i > (uint64_t)A(1).i ? A(0) : A(1);
}

const ak_lib_entry ak_int_lib[] = {
	{"int.abs", int_abs},
	{"int.exactly", int_exactly},
	{"int.exactly_float", int_exactly_float},
	{"int.parse", int_parse},
	{"math.minu", m_minu},
	{"math.maxu", m_maxu},
	{NULL, NULL},
};
