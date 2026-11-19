// bigint_cases.h: std/bigint's builtins (core/src/std/bigint.c) tested
// through their table, BI_TABLE, as the VM calls them. bigint_test.c runs
// them on the build's limbs (64 bits on a 64-bit computer), and
// bigint32_test.c on 32-bit limbs (what wasm32, armv7 and i686 run).
//
// The large cases are checked against Python's integers: the operands
// come from splitmix64 seeded alike on both sides, every result is
// written as text, and the text's SHA-256 is Python's (the script that
// made them is in the comment above bt_vectors[]).
#include "t.h"
#include "std/std.h"

#include <time.h>

static ak_lib_fn bi_fn(const char *name) {
	char full[64];
	snprintf(full, sizeof full, "bigint.%s", name);
	for (const ak_lib_entry *e = BI_TABLE; e->name; e++)
		if (strcmp(e->name, full) == 0) return e->fn;
	fprintf(stderr, "no builtin %s\n", full);
	abort();
}

// What a test makes, released as it ends (bi_done).
static void *bi_made[1 << 14];
static int bi_nmade;

static void *bt_keep(void *o) {
	if (o) {
		if (bi_nmade == (int)(sizeof bi_made / sizeof bi_made[0])) {
			fprintf(stderr, "bigint_cases: too many objects\n");
			abort();
		}
		bi_made[bi_nmade++] = o;
	}
	return o;
}

static void bi_done(void) {
	while (bi_nmade) ak_release(bi_made[--bi_nmade]);
	if (ak_error_pending()) ak_release(ak_error_take());
}

// call calls builtin name with up to four arguments.
static ak_val bt_call(const char *name, ak_val a, ak_val b, ak_val c) {
	ak_val args[3] = {a, b, c};
	return bi_fn(name)(args, 3);
}

#define V(x) ((ak_val){.p = (x)})
#define I(x) ((ak_val){.i = (x)})
#define Z ((ak_val){0})

typedef ak_bytes bt_big;

static bt_big *bt_op2(const char *name, bt_big *a, bt_big *b) { return bt_keep(bt_call(name, V(a), V(b), Z).p); }
static bt_big *bt_op1(const char *name, bt_big *a) { return bt_keep(bt_call(name, V(a), Z, Z).p); }
static bt_big *bt_opi(const char *name, bt_big *a, int64_t i) { return bt_keep(bt_call(name, V(a), I(i), Z).p); }

static bt_big *bt_parse_r(const char *s, int radix) { return bt_keep(bt_call("bigParse", V(bt_keep(ak_str_c(s))), I(radix), Z).p); }
static bt_big *bt_num(const char *s) {
	bt_big *b = bt_parse_r(s, 0);
	if (!b) {
		fprintf(stderr, "not a number: %s\n", s);
		abort();
	}
	return b;
}
static const char *bt_text_r(bt_big *a, int radix) { return ((ak_str *)bt_keep(bt_call("bigText", V(a), I(radix), Z).p))->data; }
static const char *bt_text(bt_big *a) { return bt_text_r(a, 10); }
static bt_big *bt_of(int64_t v) { return bt_keep(bt_call("bigFromInt", I(v), Z, Z).p); }
static int64_t bt_cmp(bt_big *a, bt_big *b) { return bt_call("bigCmp", V(a), V(b), Z).i; }

// thrown is the message of the error pending (none: NULL), taken.
static char bi_msg[256];
static const char *bt_thrown(void) {
	if (!ak_error_pending()) return NULL;
	void *e = ak_error_take();
	snprintf(bi_msg, sizeof bi_msg, "%s", ak_error_message(e)->data);
	ak_release(e);
	return bi_msg;
}

#define CHECK_TEXT(x, want) CHECK_EQ_STR(bt_text(x), want)

TEST(text_round_trips) {
	CHECK_TEXT(bt_num("0"), "0");
	CHECK_TEXT(bt_num("-0"), "0");
	CHECK_EQ_INT(bt_num("-0")->len, 0);
	CHECK_TEXT(bt_num("+42"), "42");
	CHECK_TEXT(bt_num("-1_000_000"), "-1000000");
	CHECK_TEXT(bt_num("0xff"), "255");
	CHECK_TEXT(bt_num("-0x_dead_beef"), "-3735928559");
	CHECK_TEXT(bt_num("0o17"), "15");
	CHECK_TEXT(bt_num("0b1010"), "10");
	CHECK_TEXT(bt_num("007"), "7");
	CHECK_TEXT(bt_parse_r("ff", 16), "255");
	CHECK_TEXT(bt_parse_r("0xff", 16), "255");
	CHECK_TEXT(bt_parse_r("0b1", 16), "177");
	CHECK_TEXT(bt_parse_r("zz", 36), "1295");
	CHECK_TEXT(bt_parse_r("ZZ", 36), "1295");
	CHECK(bt_parse_r("", 10) == NULL);
	CHECK(bt_parse_r("-", 10) == NULL);
	CHECK(bt_parse_r("1_", 10) == NULL);
	CHECK(bt_parse_r("_1", 10) == NULL);
	CHECK(bt_parse_r("1__0", 10) == NULL);
	CHECK(bt_parse_r("12a", 10) == NULL);
	CHECK(bt_parse_r(" 1", 10) == NULL);
	CHECK(bt_parse_r("0x", 0) == NULL);
	CHECK(bt_parse_r("2", 2) == NULL);
	CHECK(bt_parse_r("1", 37) == NULL);
	CHECK_EQ_STR(bt_thrown(), "bigint: radix 37 isn't 2 to 36");
	bt_big *x = bt_num("-123456789012345678901234567890");
	CHECK_EQ_STR(bt_text_r(x, 16), "-18ee90ff6c373e0ee4e3f0ad2");
	CHECK_EQ_STR(bt_text_r(x, 2), "-1100011101110100100001111111101101100001101110011111000001110111001001110001111110000101011010010");
	CHECK_EQ_STR(bt_text_r(x, 36), "-byw97um9s91dlz68tsi");
	for (int r = 2; r <= 36; r++) {
		const char *t = bt_text_r(x, r);
		CHECK(bt_cmp(bt_parse_r(t, r), x) == 0);
	}
	// Python: int('9' * 400) in radix 7, and back.
	char nines[401];
	memset(nines, '9', 400);
	nines[400] = 0;
	bt_big *n = bt_num(nines);
	CHECK_EQ_STR(bt_text(n), nines);
	CHECK(bt_cmp(bt_parse_r(bt_text_r(n, 7), 7), n) == 0);
	CHECK(bt_cmp(bt_parse_r(bt_text_r(n, 32), 32), n) == 0);
	bi_done();
}

TEST(ints_and_floats) {
	CHECK_TEXT(bt_of(INT64_MIN), "-9223372036854775808");
	CHECK_TEXT(bt_of(INT64_MAX), "9223372036854775807");
	ak_box *b = bt_keep(bt_call("bigToInt", V(bt_of(INT64_MIN)), Z, Z).p);
	CHECK(b && b->v.i == INT64_MIN);
	b = bt_keep(bt_call("bigToInt", V(bt_num("9223372036854775807")), Z, Z).p);
	CHECK(b && b->v.i == INT64_MAX);
	CHECK(bt_call("bigToInt", V(bt_num("9223372036854775808")), Z, Z).p == NULL);
	CHECK(bt_call("bigToInt", V(bt_num("-9223372036854775809")), Z, Z).p == NULL);
	b = bt_keep(bt_call("bigToInt", V(bt_num("0")), Z, Z).p);
	CHECK(b && b->v.i == 0);
	CHECK(bt_call("bigToFloat", V(bt_num("0")), Z, Z).f == 0);
	CHECK(bt_call("bigToFloat", V(bt_num("-12345")), Z, Z).f == -12345.0);
	// 2^53 + 1 is a tie: to even, 2^53; 2^53 + 3 up, to 2^53 + 4.
	CHECK(bt_call("bigToFloat", V(bt_num("9007199254740993")), Z, Z).f == 9007199254740992.0);
	CHECK(bt_call("bigToFloat", V(bt_num("9007199254740995")), Z, Z).f == 9007199254740996.0);
	// A tie past 64 bits that a 1 far below breaks: up.
	bt_big *t = bt_op2("bigAdd", bt_opi("bigShl", bt_num("0x20000000000001"), 100), bt_opi("bigShl", bt_of(1), 3));
	CHECK(bt_call("bigToFloat", V(t), Z, Z).f == ldexp(9007199254740994.0, 100));
	CHECK(isinf(bt_call("bigToFloat", V(bt_opi("bigShl", bt_of(1), 1024)), Z, Z).f));
	CHECK(bt_call("bigToFloat", V(bt_opi("bigShl", bt_of(-1), 1023)), Z, Z).f == -ldexp(1, 1023));
	CHECK_TEXT(bt_keep(bt_call("bigFromFloat", (ak_val){.f = -2.9}, Z, Z).p), "-2");
	CHECK_TEXT(bt_keep(bt_call("bigFromFloat", (ak_val){.f = 1e30}, Z, Z).p), "1000000000000000019884624838656");
	CHECK_TEXT(bt_keep(bt_call("bigFromFloat", (ak_val){.f = 0.5}, Z, Z).p), "0");
	CHECK(bt_call("bigFromFloat", (ak_val){.f = NAN}, Z, Z).p == NULL);
	CHECK(bt_call("bigFromFloat", (ak_val){.f = INFINITY}, Z, Z).p == NULL);
	bi_done();
}

static bt_big *bt_bytes_of(const uint8_t *p, size_t n) { return bt_keep(ak_bytes_new(p, n)); }

static const char *bt_hex_bytes(bt_big *b) {
	static char out[512];
	size_t o = 0;
	for (uint32_t i = 0; i < b->len && o + 3 < sizeof out; i++) o += (size_t)snprintf(out + o, sizeof out - o, "%02x", b->data[i]);
	out[o] = 0;
	return out;
}

static bt_big *bt_to_bytes(bt_big *a, int64_t size, bool sig) { return bt_keep(bt_call("bigToBytes", V(a), I(size), I(sig)).p); }
static bt_big *bt_from_bytes(bt_big *b, bool sig) { return bt_keep(bt_call("bigFromBytes", V(b), I(sig), Z).p); }

TEST(bytes) {
	CHECK_EQ_STR(bt_hex_bytes(bt_to_bytes(bt_num("0"), 0, false)), "");
	CHECK_EQ_STR(bt_hex_bytes(bt_to_bytes(bt_num("0"), 0, true)), "00");
	CHECK_EQ_STR(bt_hex_bytes(bt_to_bytes(bt_num("255"), 0, false)), "ff");
	CHECK_EQ_STR(bt_hex_bytes(bt_to_bytes(bt_num("255"), 0, true)), "00ff");
	CHECK_EQ_STR(bt_hex_bytes(bt_to_bytes(bt_num("-1"), 0, true)), "ff");
	CHECK_EQ_STR(bt_hex_bytes(bt_to_bytes(bt_num("-128"), 0, true)), "80");
	CHECK_EQ_STR(bt_hex_bytes(bt_to_bytes(bt_num("-129"), 0, true)), "ff7f");
	CHECK_EQ_STR(bt_hex_bytes(bt_to_bytes(bt_num("-2"), 4, true)), "fffffffe");
	CHECK_EQ_STR(bt_hex_bytes(bt_to_bytes(bt_num("0x1234"), 4, false)), "00001234");
	CHECK_EQ_STR(bt_hex_bytes(bt_to_bytes(bt_num("0x0102030405060708090a"), 0, false)), "0102030405060708090a");
	CHECK(bt_to_bytes(bt_num("65536"), 2, false) == NULL);
	CHECK_EQ_STR(bt_thrown(), "bigint: the number doesn't fit in 2 bytes");
	CHECK(bt_to_bytes(bt_num("-1"), 0, false) == NULL);
	CHECK(bt_thrown() != NULL);
	CHECK(bt_to_bytes(bt_num("128"), 1, true) == NULL);
	CHECK(bt_thrown() != NULL);
	const uint8_t ff[] = {0xff, 0xff}, x80[] = {0x80}, mixed[] = {0x00, 0x00, 0x7f, 0x01};
	CHECK_TEXT(bt_from_bytes(bt_bytes_of(ff, 2), false), "65535");
	CHECK_TEXT(bt_from_bytes(bt_bytes_of(ff, 2), true), "-1");
	CHECK_TEXT(bt_from_bytes(bt_bytes_of(x80, 1), true), "-128");
	CHECK_TEXT(bt_from_bytes(bt_bytes_of(mixed, 4), true), "32513");
	CHECK_TEXT(bt_from_bytes(bt_bytes_of(NULL, 0), true), "0");
	// Round trips, signed and not, of numbers about a limb's edges.
	const char *xs[] = {"1", "-1", "127", "128", "-128", "-129", "0xffffffff", "-0xffffffff", "0x100000000", "-0x100000000",
		"0xffffffffffffffff", "-0x8000000000000000", "-0x8000000000000001", "0x123456789abcdef0123456789abcdef", "-0x123456789abcdef0123456789abcdef"};
	for (size_t i = 0; i < sizeof xs / sizeof xs[0]; i++) {
		bt_big *x = bt_num(xs[i]);
		CHECK(bt_cmp(bt_from_bytes(bt_to_bytes(x, 0, true), true), x) == 0);
		CHECK(bt_cmp(bt_from_bytes(bt_to_bytes(x, 40, true), true), x) == 0);
		if (xs[i][0] != '-') CHECK(bt_cmp(bt_from_bytes(bt_to_bytes(x, 0, false), false), x) == 0);
	}
	bi_done();
}

static bt_big *bt_quo(bt_big *a, bt_big *b) { return bt_keep(bt_call("bigDiv", V(a), V(b), I(0)).p); }
static bt_big *bt_rem(bt_big *a, bt_big *b) { return bt_keep(bt_call("bigDiv", V(a), V(b), I(1)).p); }
static bt_big *bt_fdiv(bt_big *a, bt_big *b) { return bt_keep(bt_call("bigDiv", V(a), V(b), I(2)).p); }
static bt_big *bt_fmod_(bt_big *a, bt_big *b) { return bt_keep(bt_call("bigDiv", V(a), V(b), I(3)).p); }

TEST(division_signs) {
	// As Go's Quo and Rem (and C's / and %), and Python's // and %.
	struct {
		const char *a, *b, *q, *r, *fq, *fr;
	} cs[] = {
		{"7", "2", "3", "1", "3", "1"},
		{"-7", "2", "-3", "-1", "-4", "1"},
		{"7", "-2", "-3", "1", "-4", "-1"},
		{"-7", "-2", "3", "-1", "3", "-1"},
		{"6", "-2", "-3", "0", "-3", "0"},
		{"0", "-5", "0", "0", "0", "0"},
		{"1", "100000000000000000000000000000", "0", "1", "0", "1"},
		{"-1", "100000000000000000000000000000", "0", "-1", "-1", "99999999999999999999999999999"},
		{"-100000000000000000000000000000", "7", "-14285714285714285714285714285", "-5", "-14285714285714285714285714286", "2"},
	};
	for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
		bt_big *a = bt_num(cs[i].a), *b = bt_num(cs[i].b);
		CHECK_TEXT(bt_quo(a, b), cs[i].q);
		CHECK_TEXT(bt_rem(a, b), cs[i].r);
		CHECK_TEXT(bt_fdiv(a, b), cs[i].fq);
		CHECK_TEXT(bt_fmod_(a, b), cs[i].fr);
	}
	CHECK(bt_quo(bt_num("5"), bt_num("0")) == NULL);
	CHECK_EQ_STR(bt_thrown(), "division by zero");
	CHECK(bt_fmod_(bt_num("5"), bt_num("-0")) == NULL);
	CHECK_EQ_STR(bt_thrown(), "division by zero");
	ak_arr *both = bt_keep(bt_call("bigDivBoth", V(bt_num("-7")), V(bt_num("2")), I(1)).p);
	CHECK_EQ_INT(both->len, 2);
	CHECK_TEXT(both->data[0].p, "-4");
	CHECK_TEXT(both->data[1].p, "1");
	bi_done();
}

TEST(knuth_edge_cases) {
	// Divisions whose quotient digit is first guessed one or two too bt_big
	// (Knuth's add back), at 32 and 64-bit limbs.
	const char *cs[][2] = {
		{"0x7fffffff800000010000000000000000", "0x800000008000000200000005"},
		{"0x7fffffffffffffff8000000000000001000000000000000000000000000000000", "0x80000000000000008000000000000002000000000000000005"},
		{"0x10000000000000000000000000000000000000000", "0x100000000000000000000000000000001"},
		{"0xffffffffffffffffffffffffffffffffffffffffffffffff", "0xffffffffffffffffffffffff"},
		{"0x1000000000000000000000000000000000000000000000000", "0xffffffffffffffffffffffffffffffff"},
	};
	for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
		bt_big *a = bt_num(cs[i][0]), *b = bt_num(cs[i][1]);
		bt_big *q = bt_quo(a, b), *r = bt_rem(a, b);
		CHECK(bt_cmp(bt_op2("bigAdd", bt_op2("bigMul", q, b), r), a) == 0);
		CHECK(bt_cmp(r, b) < 0 && bt_cmp(r, bt_of(0)) >= 0);
	}
	bi_done();
}

TEST(known_values) {
	// 2^100, and 2^64 - 1 squared.
	CHECK_TEXT(bt_opi("bigPow", bt_of(2), 100), "1267650600228229401496703205376");
	CHECK_TEXT(bt_opi("bigPow", bt_of(-3), 3), "-27");
	CHECK_TEXT(bt_opi("bigPow", bt_of(-1), 1000001), "-1");
	CHECK_TEXT(bt_opi("bigPow", bt_of(0), 0), "1");
	CHECK_TEXT(bt_opi("bigPow", bt_of(7), 0), "1");
	CHECK(bt_opi("bigPow", bt_of(2), -1) == NULL);
	CHECK(bt_thrown() != NULL);
	bt_big *m = bt_num("18446744073709551615");
	CHECK_TEXT(bt_op2("bigMul", m, m), "340282366920938463426481119284349108225");
	CHECK_TEXT(bt_keep(bt_call("bigFactorial", I(20), Z, Z).p), "2432902008176640000");
	CHECK_TEXT(bt_keep(bt_call("bigFactorial", I(0), Z, Z).p), "1");
	CHECK(bt_call("bigFactorial", I(-1), Z, Z).p == NULL);
	CHECK(bt_thrown() != NULL);
	// Fibonacci by additions.
	bt_big *a = bt_of(0), *b = bt_of(1);
	for (int i = 0; i < 1000; i++) {
		bt_big *t = bt_op2("bigAdd", a, b);
		a = b, b = t;
	}
	CHECK_TEXT(a, "43466557686937456435688527675040625802564660517371780402481729089536555417949051890403879840079255169295922593080322634775209689623239873322471161642996440906533187938298969649928516003704476137795166849228875");
	bi_done();
}

static void bt_sha(const char *s, size_t n, char out[65]) {
	uint8_t h[32];
	ak_sha256(s, n, h);
	for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", h[i]);
}

static void bt_check_sha(const char *s, const char *want) {
	char got[65];
	bt_sha(s, strlen(s), got);
	CHECK_EQ_STR(got, want);
}

TEST(factorials_and_fibonacci) {
	// SHA-256 of the decimal text, from Python.
	bt_big *f = bt_keep(bt_call("bigFactorial", I(1000), Z, Z).p);
	bt_check_sha(bt_text(f), "cc336cf135d690c1105664b3b859db66b940db51cd66cf891fee120584cf7873");
	f = bt_keep(bt_call("bigFactorial", I(10000), Z, Z).p);
	const char *t = bt_text(f);
	CHECK_EQ_INT(strlen(t), 35660);
	bt_check_sha(t, "f17f312be51414ec57f0fda0b1589003663a505df1ef2aafac40bb3be2e46c8e");
	CHECK(bt_cmp(bt_num(t), f) == 0);
	bt_big *a = bt_of(0), *b = bt_of(1);
	for (int i = 0; i < 10000; i++) {
		bt_big *s = bt_op2("bigAdd", a, b);
		a = b, b = s;
	}
	bt_check_sha(bt_text(a), "e9c83559a05641cfd86d6c192c53fdfda87b8e53470b63dc19d1d0d7526e987a");
	bt_big *mers = bt_op2("bigSub", bt_opi("bigShl", bt_of(1), 4423), bt_of(1));
	bt_check_sha(bt_text(mers), "5c6c10ad2e971f207b1a7b31e1bfc3b1f608fb99f01f8f3d7bcda27474174bd9");
	bi_done();
}

TEST(primes) {
	CHECK(!bt_call("bigIsPrime", V(bt_of(-7)), I(0), Z).i);
	CHECK(!bt_call("bigIsPrime", V(bt_of(0)), I(0), Z).i);
	CHECK(!bt_call("bigIsPrime", V(bt_of(1)), I(0), Z).i);
	CHECK(bt_call("bigIsPrime", V(bt_of(2)), I(0), Z).i);
	CHECK(bt_call("bigIsPrime", V(bt_of(997)), I(0), Z).i);
	CHECK(!bt_call("bigIsPrime", V(bt_of(561)), I(20), Z).i);
	CHECK(bt_call("bigIsPrime", V(bt_of(1000003)), I(20), Z).i);
	CHECK(!bt_call("bigIsPrime", V(bt_of(1009LL * 1013)), I(20), Z).i);
	// Strong pseudoprimes to base 2 with no factor under 1000: Miller-Rabin
	// to base 2 passes them, the Lucas test doesn't.
	const char *psp[] = {"25326001", "2152302898747", "341550071728321", "3825123056546413051"};
	for (size_t i = 0; i < 4; i++) CHECK(!bt_call("bigIsPrime", V(bt_num(psp[i])), I(0), Z).i);
	int mersenne[] = {61, 89, 107, 127, 521, 607, 1279, 2203};
	for (size_t i = 0; i < sizeof mersenne / sizeof mersenne[0]; i++) {
		bt_big *p = bt_op2("bigSub", bt_opi("bigShl", bt_of(1), mersenne[i]), bt_of(1));
		CHECK(bt_call("bigIsPrime", V(p), I(mersenne[i] < 1000 ? 20 : 2), Z).i);
		CHECK(!bt_call("bigIsPrime", V(bt_op2("bigAdd", p, bt_of(2))), I(2), Z).i || mersenne[i] == 61);
	}
	bt_big *p = bt_op2("bigSub", bt_opi("bigShl", bt_of(1), 521), bt_of(1)), *q = bt_op2("bigSub", bt_opi("bigShl", bt_of(1), 607), bt_of(1));
	CHECK(!bt_call("bigIsPrime", V(bt_op2("bigMul", p, q)), I(0), Z).i);
	CHECK(!bt_call("bigIsPrime", V(bt_op2("bigMul", p, p)), I(0), Z).i);
	// 2^64 - 59, the largest prime under 2^64; and the square of a prime
	// above 1000 (Lucas would find no D).
	CHECK(bt_call("bigIsPrime", V(bt_num("18446744073709551557")), I(0), Z).i);
	CHECK(!bt_call("bigIsPrime", V(bt_num("1018081")), I(0), Z).i);
	CHECK(!bt_call("bigIsPrime", V(bt_op2("bigMul", bt_num("4294967291"), bt_num("4294967291"))), I(0), Z).i);
	// Numbers about 10^12, against trial division.
	for (int64_t n = 1000000000001; n < 1000000000001 + 400; n += 2) {
		bool want = true;
		for (int64_t d = 3; d * d <= n; d += 2)
			if (n % d == 0) {
				want = false;
				break;
			}
		CHECK_EQ_INT(bt_call("bigIsPrime", V(bt_of(n)), I(4), Z).i, want);
	}
	bi_done();
}

TEST(rsa) {
	// p = 2^607 - 1, q = 2^1279 - 1, e = 65537: c = m^e mod n, from
	// Python, and back with d = e^-1 mod (p-1)(q-1).
	bt_big *one = bt_of(1);
	bt_big *p = bt_op2("bigSub", bt_opi("bigShl", one, 607), one), *q = bt_op2("bigSub", bt_opi("bigShl", one, 1279), one);
	bt_big *n = bt_op2("bigMul", p, q), *e = bt_of(65537);
	bt_big *phi = bt_op2("bigMul", bt_op2("bigSub", p, one), bt_op2("bigSub", q, one));
	bt_big *d = bt_op2("bigModInverse", e, phi);
	REQUIRE(d);
	bt_check_sha(bt_text_r(d, 16), "695ad3335a0d7f29ffdba7cd2afe69d707e66a628f6c368713b876ac255c818f");
	bt_big *msg = bt_num("0x48656c6c6f2c20417375b6b61");
	bt_big *c = bt_keep(bt_call("bigModPow", V(msg), V(e), V(n)).p);
	CHECK_EQ_STR(bt_text_r(c, 16), "37f8abca2f785acf7f8083688c4044b2bab9eaa2ce6b24239418c79709809fca267eccd221919bdf36bbe018e2e458216bed2a35d6f19635c7504b26a3afe6d1a0891c2bc78178c1c7990d2de77c1bd2a3cc68cf8b06e8f28351f6d2fed2df5d0298d837a45f13983fef38d6af6fed543d2ab62d4b1e17356fca8225f4481a4c87db45aec0454a1f1cfd43949995fe7996f819142447da1011af78b5d6f071c321baef01c67953fe61ae04de4a5e1b9ee61d206f919fac77c6122ed485f9221539d83fedb299a9d6f81b7f89e503900bb74c9f33cefba78b909f23f05ddfe640c49960a2438777333069e59a");
	CHECK(bt_cmp(bt_keep(bt_call("bigModPow", V(c), V(d), V(n)).p), msg) == 0);
	// A negative exponent is the inverse's power.
	bt_big *inv = bt_keep(bt_call("bigModPow", V(e), V(bt_of(-1)), V(phi)).p);
	CHECK(bt_cmp(inv, d) == 0);
	CHECK_TEXT(bt_keep(bt_call("bigModPow", V(bt_of(-3)), V(bt_of(3)), V(bt_of(7))).p), "1");
	CHECK_TEXT(bt_keep(bt_call("bigModPow", V(bt_of(5)), V(bt_of(0)), V(bt_of(1))).p), "0");
	CHECK_TEXT(bt_keep(bt_call("bigModPow", V(bt_of(5)), V(bt_of(0)), V(bt_of(10))).p), "1");
	CHECK_TEXT(bt_keep(bt_call("bigModPow", V(bt_of(3)), V(bt_of(200)), V(bt_of(1000))).p), "1");
	CHECK(bt_call("bigModPow", V(bt_of(2)), V(bt_of(3)), V(bt_of(0))).p == NULL);
	CHECK_EQ_STR(bt_thrown(), "division by zero");
	CHECK(bt_call("bigModPow", V(bt_of(2)), V(bt_of(3)), V(bt_of(-5))).p == NULL);
	CHECK(bt_thrown() != NULL);
	CHECK(bt_call("bigModPow", V(bt_of(2)), V(bt_of(-1)), V(bt_of(4))).p == NULL);
	CHECK(bt_thrown() != NULL);
	CHECK(bt_op2("bigModInverse", bt_of(2), bt_of(4)) == NULL);
	CHECK(!ak_error_pending());
	CHECK_TEXT(bt_op2("bigModInverse", bt_of(-3), bt_of(7)), "2");
	CHECK_TEXT(bt_op2("bigGcd", bt_of(0), bt_of(0)), "0");
	CHECK_TEXT(bt_op2("bigGcd", bt_of(-12), bt_of(18)), "6");
	CHECK_TEXT(bt_op2("bigLcm", bt_of(-4), bt_of(6)), "12");
	CHECK_TEXT(bt_op2("bigLcm", bt_of(0), bt_of(6)), "0");
	bi_done();
}

TEST(bits) {
	// Two's complement, as Python's.
	CHECK_TEXT(bt_op2("bigAnd", bt_of(-6), bt_of(13)), "8");
	CHECK_TEXT(bt_op2("bigAnd", bt_of(-6), bt_of(-13)), "-14");
	CHECK_TEXT(bt_op2("bigOr", bt_of(-6), bt_of(13)), "-1");
	CHECK_TEXT(bt_op2("bigOr", bt_of(6), bt_of(-13)), "-9");
	CHECK_TEXT(bt_op2("bigXor", bt_of(-6), bt_of(13)), "-9");
	CHECK_TEXT(bt_op2("bigXor", bt_of(-6), bt_of(-13)), "9");
	CHECK_TEXT(bt_op1("bigNot", bt_of(0)), "-1");
	CHECK_TEXT(bt_op1("bigNot", bt_of(-1)), "0");
	CHECK_TEXT(bt_opi("bigShr", bt_of(-1), 1000), "-1");
	CHECK_TEXT(bt_opi("bigShr", bt_of(-5), 1), "-3");
	CHECK_TEXT(bt_opi("bigShr", bt_of(5), 1), "2");
	CHECK_TEXT(bt_opi("bigShr", bt_of(5), 100), "0");
	CHECK_TEXT(bt_opi("bigShl", bt_of(-5), 3), "-40");
	CHECK(bt_opi("bigShl", bt_of(1), -1) == NULL);
	CHECK(bt_thrown() != NULL);
	CHECK(bt_opi("bigShl", bt_of(1), (int64_t)1 << 31) == NULL);
	CHECK_EQ_STR(bt_thrown(), "bigint: too large: more than 2^30 bits");
	CHECK_TEXT(bt_opi("bigShl", bt_of(0), (int64_t)1 << 40), "0");
	CHECK_EQ_INT(bt_call("bigBitLength", V(bt_of(255)), Z, Z).i, 8);
	CHECK_EQ_INT(bt_call("bigBitLength", V(bt_of(-256)), Z, Z).i, 9);
	CHECK_EQ_INT(bt_call("bigBitLength", V(bt_of(0)), Z, Z).i, 0);
	CHECK_EQ_INT(bt_call("bigBit", V(bt_of(-2)), I(0), Z).i, 0);
	CHECK_EQ_INT(bt_call("bigBit", V(bt_of(-2)), I(1), Z).i, 1);
	CHECK_EQ_INT(bt_call("bigBit", V(bt_of(-2)), I(5000), Z).i, 1);
	CHECK_EQ_INT(bt_call("bigBit", V(bt_of(5)), I(5000), Z).i, 0);
	CHECK_EQ_INT(bt_call("bigTrailingZeros", V(bt_opi("bigShl", bt_of(-3), 200)), Z, Z).i, 200);
	CHECK_EQ_INT(bt_call("bigTrailingZeros", V(bt_of(0)), Z, Z).i, 0);
	bi_done();
}

TEST(roots_and_randomness) {
	CHECK_TEXT(bt_op1("bigSqrt", bt_of(0)), "0");
	CHECK_TEXT(bt_op1("bigSqrt", bt_of(15)), "3");
	CHECK_TEXT(bt_op1("bigSqrt", bt_of(16)), "4");
	CHECK_TEXT(bt_op1("bigSqrt", bt_num("18446744073709551615")), "4294967295");
	CHECK_TEXT(bt_op1("bigSqrt", bt_num("18446744073709551616")), "4294967296");
	bt_big *ten = bt_opi("bigPow", bt_of(10), 100);
	CHECK_TEXT(bt_op1("bigSqrt", ten), "100000000000000000000000000000000000000000000000000");
	CHECK_TEXT(bt_op1("bigSqrt", bt_op2("bigSub", ten, bt_of(1))), "99999999999999999999999999999999999999999999999999");
	CHECK(bt_op1("bigSqrt", bt_of(-1)) == NULL);
	CHECK(bt_thrown() != NULL);
	bt_big *bound = bt_num("1000000000000000000000000000000");
	for (int i = 0; i < 50; i++) {
		bt_big *r = bt_op1("bigRandom", bound);
		CHECK(bt_cmp(r, bt_of(0)) >= 0 && bt_cmp(r, bound) < 0);
	}
	for (int i = 0; i < 20; i++) CHECK_TEXT(bt_op1("bigRandom", bt_of(1)), "0");
	CHECK(bt_op1("bigRandom", bt_of(0)) == NULL);
	CHECK(bt_thrown() != NULL);
	bi_done();
}

TEST(limits) {
	CHECK(bt_opi("bigPow", bt_of(2), (int64_t)1 << 31) == NULL);
	CHECK_EQ_STR(bt_thrown(), "bigint: too large: more than 2^30 bits");
	CHECK(bt_opi("bigPow", bt_of(3), INT64_MAX) == NULL);
	CHECK(bt_thrown() != NULL);
	CHECK(bt_call("bigFactorial", I(1000000000), Z, Z).p == NULL);
	CHECK(bt_thrown() != NULL);
	bi_done();
}

// ---- Against Python ----

static uint64_t bi_splitmix(uint64_t *x) {
	uint64_t z = (*x += 0x9e3779b97f4a7c15ull);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
	return z ^ (z >> 31);
}

// gen is a number of exactly bits bits (its top one set), negative when
// the generator's next is odd: bt_vectors.py's gen.
static bt_big *bt_gen(uint64_t *g, int bits) {
	int words = (bits + 63) / 64;
	uint8_t *b = ak_alloc((size_t)words * 8);
	for (int i = 0; i < words; i++) {
		uint64_t w = bi_splitmix(g);
		if (i == words - 1) {
			int top = bits - 64 * i;
			if (top < 64) w &= ((uint64_t)1 << top) - 1;
			w |= (uint64_t)1 << (top - 1);
		}
		for (int k = 0; k < 8; k++) b[(size_t)words * 8 - 1 - (size_t)(8 * i + k)] = (uint8_t)(w >> (8 * k));
	}
	bt_big *x = bt_from_bytes(bt_bytes_of(b, (size_t)words * 8), false);
	ak_free(b);
	if (bi_splitmix(g) & 1) x = bt_op1("bigNeg", x);
	return x;
}

static void bt_line(ak_buf *out, const char *s) {
	if (out->len) ak_buf_put(out, "\n", 1);
	ak_buf_put(out, s, strlen(s));
}

static void bt_line_int(ak_buf *out, long long v) {
	char t[32];
	snprintf(t, sizeof t, "%lld", v);
	bt_line(out, t);
}

static void bt_line_float(ak_buf *out, double f) {
	uint64_t u;
	memcpy(&u, &f, sizeof u);
	char t[32];
	snprintf(t, sizeof t, "%llx", (unsigned long long)u);
	bt_line(out, t);
}

// The SHA-256 of what bt_vectors.py writes for each case:
//
//   out += [hx(a + b), hx(a - b), hx(a * b)]
//   out += [hx(q), hx(a - q*b), hx(a // b), hx(a % b)]    # q toward zero
//   if max(ba, bb) <= 20000: out += [hx(gcd(a, b)), hx(lcm(a, b))]
//   out += [hx(a & b), hx(a | b), hx(a ^ b), hx(~a), hx(a >> 77), hx(a << 77)]
//   out += [hx(isqrt(|a|)), str(a*b), base36(a), floatbits(a), floatbits(a*b)]
//   out += [bitlen(|a|), trailingzeros(a*b), bit(a, 5), bit(a, ba + 3)]
//   if bm: m = |bt_gen(bm)| | 1; e = |b|
//          out += [hx(pow(a, e, m)), hx(pow(a, e, m + 1)), hx(pow(a, -1, m)) or "none"]
static const struct {
	int a, b, m;
	uint64_t seed;
	const char *sha;
} bt_vectors[] = {
	{64, 64, 64, 1, "1807a657e9c17777b6f93a705e48ce758094f83f60decbcdf72b5e7c3fde20eb"},
	{200, 70, 128, 2, "f67784652978d2b954c374d889c2af58cb5e6bafd5d6d7b8a75b1c6928768784"},
	{1000, 999, 512, 3, "e52f552fb9df1039f59a05e916cd77a125fe6c4d60317f0c8f6ab9d7f60e4ae9"},
	{3000, 1400, 1024, 4, "f8005ad6fed16d7b5c30b34d6414a4f0f45300b109f5d5dc9a13f94e04e0ca4d"},
	{5000, 2600, 2048, 5, "dd96bda0256c058e76474599df147e0f81699520674985b6d171251a2fca5522"},
	{9000, 4500, 0, 6, "9372774bdf06daeb18dfcba008bfd015d3c6c60e40732b193f2027e3128aa874"},
	{20000, 9000, 0, 7, "1c79a38a362f50fdeba98fe9271b02eeb04b98f79758ceaaa176a241624a140e"},
	{60000, 30000, 0, 8, "0a0ac4b0956f496564382a14ea764ba8e31feae72acb6840a7504a3407a06c4e"},
	{200000, 90000, 0, 9, "c2dac8c8a7737a7f88cfbcc5b55afa72baaf9a167295ace89779f9ff17da0fce"},
	{100000, 7000, 0, 10, "c91a51ed09ded1ddccf41a2d90d868da55e11369dd4532e2a9029589ff6f3bc6"},
	{7000, 100000, 0, 11, "fb1576a42519b740cb45cb442efb46ee445fad7a7d350e352143ed2bbdbdaddb"},
	{300000, 300000, 0, 12, "974582826a45dc9fb96db5024ef5767b6584b1598b53da63450a6908507d639e"},
	{130, 1, 0, 13, "58ba34a8e545a1fe1d2858ab05743146a6fa3d59e949807d588568a9e37dc1bc"},
	{65, 64, 0, 14, "983c03ef15e3f5317e8e925dc9972bcbec23fc2267dff8d670dbd864aad6e1b3"},
};

TEST(against_python) {
	for (size_t v = 0; v < sizeof bt_vectors / sizeof bt_vectors[0]; v++) {
		uint64_t g = bt_vectors[v].seed;
		bt_big *a = bt_gen(&g, bt_vectors[v].a), *b = bt_gen(&g, bt_vectors[v].b);
		ak_buf out = {0};
		bt_big *p = bt_op2("bigMul", a, b);
		bt_line(&out, bt_text_r(bt_op2("bigAdd", a, b), 16));
		bt_line(&out, bt_text_r(bt_op2("bigSub", a, b), 16));
		bt_line(&out, bt_text_r(p, 16));
		bt_line(&out, bt_text_r(bt_quo(a, b), 16));
		bt_line(&out, bt_text_r(bt_rem(a, b), 16));
		bt_line(&out, bt_text_r(bt_fdiv(a, b), 16));
		bt_line(&out, bt_text_r(bt_fmod_(a, b), 16));
		if (bt_vectors[v].a <= 20000 && bt_vectors[v].b <= 20000) {
			bt_line(&out, bt_text_r(bt_op2("bigGcd", a, b), 16));
			bt_line(&out, bt_text_r(bt_op2("bigLcm", a, b), 16));
		}
		bt_line(&out, bt_text_r(bt_op2("bigAnd", a, b), 16));
		bt_line(&out, bt_text_r(bt_op2("bigOr", a, b), 16));
		bt_line(&out, bt_text_r(bt_op2("bigXor", a, b), 16));
		bt_line(&out, bt_text_r(bt_op1("bigNot", a), 16));
		bt_line(&out, bt_text_r(bt_opi("bigShr", a, 77), 16));
		bt_line(&out, bt_text_r(bt_opi("bigShl", a, 77), 16));
		bt_line(&out, bt_text_r(bt_op1("bigSqrt", bt_op1("bigAbs", a)), 16));
		const char *dec = bt_text(p);
		bt_line(&out, dec);
		CHECK(bt_cmp(bt_num(dec), p) == 0);
		const char *t36 = bt_text_r(a, 36);
		bt_line(&out, t36);
		CHECK(bt_cmp(bt_parse_r(t36, 36), a) == 0);
		bt_line_float(&out, bt_call("bigToFloat", V(a), Z, Z).f);
		bt_line_float(&out, bt_call("bigToFloat", V(p), Z, Z).f);
		bt_line_int(&out, bt_call("bigBitLength", V(a), Z, Z).i);
		bt_line_int(&out, bt_call("bigTrailingZeros", V(p), Z, Z).i);
		bt_line_int(&out, bt_call("bigBit", V(a), I(5), Z).i);
		bt_line_int(&out, bt_call("bigBit", V(a), I(bt_vectors[v].a + 3), Z).i);
		if (bt_vectors[v].m) {
			bt_big *m = bt_op2("bigOr", bt_op1("bigAbs", bt_gen(&g, bt_vectors[v].m)), bt_of(1));
			bt_big *e = bt_op1("bigAbs", b);
			bt_line(&out, bt_text_r(bt_keep(bt_call("bigModPow", V(a), V(e), V(m)).p), 16));
			bt_line(&out, bt_text_r(bt_keep(bt_call("bigModPow", V(a), V(e), V(bt_op2("bigAdd", m, bt_of(1)))).p), 16));
			bt_big *inv = bt_op2("bigModInverse", a, m);
			bt_line(&out, inv ? bt_text_r(inv, 16) : "none");
		}
		char got[65];
		bt_sha((const char *)out.data, out.len, got);
		if (strcmp(got, bt_vectors[v].sha) != 0) T_FAIL("case %zu (%d, %d bits): %s, want %s", v, bt_vectors[v].a, bt_vectors[v].b, got, bt_vectors[v].sha);
		ak_buf_free(&out);
		bi_done();
	}
}

// Random round trips: q·b + r = a with |r| < |b| and r's sign a's, at
// sizes about every threshold, and text and bytes back.
TEST(round_trips) {
	uint64_t g = 99;
	int sizes[] = {1, 31, 32, 33, 63, 64, 65, 100, 1279, 1281, 2559, 2561, 4095, 4097, 5000, 8191, 8193, 12000, 40000};
	size_t ns = sizeof sizes / sizeof sizes[0];
	for (size_t i = 0; i < ns; i++) {
		for (size_t j = 0; j < ns; j++) {
			bt_big *a = bt_gen(&g, sizes[i]), *b = bt_gen(&g, sizes[j]);
			bt_big *q = bt_quo(a, b), *r = bt_rem(a, b);
			CHECK(bt_cmp(bt_op2("bigAdd", bt_op2("bigMul", q, b), r), a) == 0);
			CHECK(bt_cmp(bt_op1("bigAbs", r), bt_op1("bigAbs", b)) < 0);
			CHECK(r->len == 0 || r->data[0] == a->data[0]);
			// (a·b)/b = a, and a square by its own path is a product's.
			bt_big *p = bt_op2("bigMul", a, b);
			CHECK(bt_cmp(bt_quo(p, b), a) == 0);
			CHECK(bt_rem(p, b)->len == 0);
			CHECK(bt_cmp(bt_op2("bigMul", a, a), bt_op2("bigMul", a, bt_op2("bigAdd", a, bt_of(0)))) == 0);
			if (i == j) {
				CHECK(bt_cmp(bt_op1("bigSqrt", bt_op2("bigMul", a, a)), bt_op1("bigAbs", a)) == 0);
			}
			bi_done();
		}
		bt_big *a = bt_gen(&g, sizes[i]);
		for (int r = 2; r <= 36; r += 5) CHECK(bt_cmp(bt_parse_r(bt_text_r(a, r), r), a) == 0);
		CHECK(bt_cmp(bt_from_bytes(bt_to_bytes(a, 0, true), true), a) == 0);
		bi_done();
	}
}

// What JSON writes and reads of a BigInt (json.c's hook): its digits.
TEST(json_hooks) {
	const ak_bigint_json *h = &BI_JSON;
	CHECK(h->is("std/bigint.BigInt", 17));
	CHECK(!h->is("std/bigint.BigInts", 18));
	CHECK(!h->is("app.BigInt", 10));
	ak_strb sb = {0};
	h->write(&sb, bt_num("-123456789012345678901234567890"));
	ak_str *s = bt_keep(ak_strb_finish(&sb));
	CHECK_EQ_STR(s->data, "-123456789012345678901234567890");
	bt_big *b = bt_keep(h->read("-98765432109876543210", 21));
	REQUIRE(b);
	CHECK_TEXT(b, "-98765432109876543210");
	bi_done();
}

static double bt_now(void) {
	struct timespec t;
	timespec_get(&t, TIME_UTC);
	return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

// Bench: AK_BIGINT_BENCH=1 ./bigint_test bench (an optimized build).
TEST(bench) {
	if (!getenv("AK_BIGINT_BENCH")) return;
	double t0 = bt_now();
	bt_big *f = bt_keep(bt_call("bigFactorial", I(100000), Z, Z).p);
	double t1 = bt_now();
	const char *ft = bt_text(f);
	double t2 = bt_now();
	fprintf(stderr, "100000!: %.3fs; its %zu digits printed: %.3fs\n", t1 - t0, strlen(ft), t2 - t1);
	bt_big *x = bt_op2("bigSub", bt_opi("bigPow", bt_of(10), 1000000), bt_of(1));
	t0 = bt_now();
	const char *xt = bt_text(x);
	t1 = bt_now();
	bt_big *y = bt_num(xt);
	t2 = bt_now();
	CHECK(bt_cmp(x, y) == 0);
	fprintf(stderr, "10^1000000 - 1: printed %.3fs, parsed %.3fs\n", t1 - t0, t2 - t1);
	t0 = bt_now();
	bt_big *sq = bt_op2("bigMul", x, y);
	t1 = bt_now();
	bt_big *back = bt_quo(sq, x);
	t2 = bt_now();
	CHECK(bt_cmp(back, y) == 0);
	fprintf(stderr, "a million digits squared: %.3fs; divided back: %.3fs\n", t1 - t0, t2 - t1);
	uint64_t g = 7;
	bt_big *m = bt_op2("bigOr", bt_op1("bigAbs", bt_gen(&g, 2048)), bt_of(1)), *e = bt_op1("bigAbs", bt_gen(&g, 2048)), *base = bt_op1("bigAbs", bt_gen(&g, 2000));
	t0 = bt_now();
	for (int i = 0; i < 10; i++) bt_keep(bt_call("bigModPow", V(base), V(e), V(m)).p);
	t1 = bt_now();
	fprintf(stderr, "2048-bit modPow: %.2fms\n", (t1 - t0) * 100);
	bt_big *p = bt_op2("bigSub", bt_opi("bigShl", bt_of(1), 2203), bt_of(1));
	t0 = bt_now();
	CHECK(bt_call("bigIsPrime", V(p), I(20), Z).i);
	t1 = bt_now();
	fprintf(stderr, "isProbablePrime(2^2203 - 1, rounds: 20): %.2fms\n", (t1 - t0) * 1000);
	bt_big *a = bt_gen(&g, 100000), *b = bt_gen(&g, 100000);
	t0 = bt_now();
	bt_op2("bigGcd", a, b);
	t1 = bt_now();
	fprintf(stderr, "gcd of 100000-bit numbers: %.3fs\n", t1 - t0);
	bi_done();
}
