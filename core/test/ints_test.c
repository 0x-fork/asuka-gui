// The sized integers' instructions (vm/ops.h's N ones, the
// interpreter's and compiled C's) and rt.h's helpers: each type's range
// checked as int's is, a u64's 64 bits unsigned, values kept canonical
// (sign- or zero-extended), nothing allocated on the way.
#include "t.h"
#include "vm/ops.h"

#include <stdint.h>

static void check_overflow(bool ok) {
	CHECK(!ok);
	void *e = ak_error_take();
	REQUIRE(e != NULL);
	CHECK_EQ_STR(ak_error_message(e)->data, "integer overflow");
	ak_release(e);
}

static void check_error(bool ok, const char *msg) {
	CHECK(!ok);
	void *e = ak_error_take();
	REQUIRE(e != NULL);
	CHECK_EQ_STR(ak_error_message(e)->data, msg);
	ak_release(e);
}

TEST(ranges) {
	CHECK(ak_int_min(AK_I8) == -128 && ak_int_max(AK_I8) == 127);
	CHECK(ak_int_min(AK_I16) == -32768 && ak_int_max(AK_I16) == 32767);
	CHECK(ak_int_min(AK_I32) == INT32_MIN && ak_int_max(AK_I32) == INT32_MAX);
	CHECK(ak_int_min(AK_I64) == INT64_MIN && ak_int_max(AK_I64) == INT64_MAX);
	CHECK(ak_int_min(AK_U8) == 0 && ak_int_max(AK_U8) == 255);
	CHECK(ak_int_max(AK_U16) == 65535 && ak_int_max(AK_U32) == UINT32_MAX);
	CHECK(ak_int_max(AK_U64) == -1);  // its bits
	CHECK(ak_int_wrap(300, AK_U8) == 44 && ak_int_wrap(200, AK_I8) == -56 && ak_int_wrap(-1, AK_U16) == 65535);
	CHECK(ak_int_fits(255, AK_I64, AK_U8) && !ak_int_fits(256, AK_I64, AK_U8) && !ak_int_fits(-1, AK_I64, AK_U8));
	CHECK(!ak_int_fits(-1, AK_I64, AK_U64) && !ak_int_fits(-1, AK_U64, AK_I64) && ak_int_fits(-1, AK_U64, AK_U64));
	CHECK(ak_int_fits(INT64_MAX, AK_U64, AK_I64) && ak_int_fits(-128, AK_I8, AK_I64));
	CHECK(ak_int_clamp(300, AK_I64, AK_U8) == 255 && ak_int_clamp(-5, AK_I64, AK_U8) == 0);
	CHECK(ak_int_clamp(-1, AK_U64, AK_I32) == INT32_MAX && ak_int_clamp(INT64_MIN, AK_I64, AK_I16) == -32768);
}

TEST(arithmetic_at_each_width) {
	ak_val R[3];
	// u8: 200 + 55 fits, 200 + 56 doesn't.
	R[1].i = 200, R[2].i = 55;
	REQUIRE(ak_op_addn(R, 0, 1, 2, AK_U8));
	CHECK(R[0].i == 255);
	R[2].i = 56;
	R[0].i = 7;
	check_overflow(ak_op_addn(R, 0, 1, 2, AK_U8));
	CHECK(R[0].i == 7);  // the destination is left as it was
	// i8: -128 - 1, -(-128), -128 / -1, -128 % -1.
	R[1].i = -128, R[2].i = 1;
	check_overflow(ak_op_subn(R, 0, 1, 2, AK_I8));
	check_overflow(ak_op_negn(R, 0, 1, AK_I8));
	R[2].i = -1;
	check_overflow(ak_op_divn(R, 0, 1, 2, AK_I8, false));
	REQUIRE(ak_op_divn(R, 0, 1, 2, AK_I8, true));
	CHECK(R[0].i == 0);
	// i16, i32: their edges.
	R[1].i = 32767, R[2].i = 1;
	check_overflow(ak_op_addn(R, 0, 1, 2, AK_I16));
	R[1].i = INT32_MAX, R[2].i = 2;
	check_overflow(ak_op_muln(R, 0, 1, 2, AK_I32));
	R[1].i = 65536, R[2].i = 32768;
	REQUIRE(ak_op_muln(R, 0, 1, 2, AK_I64));
	CHECK(R[0].i == INT64_C(2147483648));
	// u32 * u32 past int64 too.
	R[1].i = UINT32_MAX, R[2].i = UINT32_MAX;
	check_overflow(ak_op_muln(R, 0, 1, 2, AK_U32));
	// u64: max + 1, 0 - 1, max * 2; max / 3 and max % 10, unsigned.
	R[1].i = -1, R[2].i = 1;
	check_overflow(ak_op_addn(R, 0, 1, 2, AK_U64));
	R[1].i = 0;
	check_overflow(ak_op_subn(R, 0, 1, 2, AK_U64));
	R[1].i = -1, R[2].i = 2;
	check_overflow(ak_op_muln(R, 0, 1, 2, AK_U64));
	R[2].i = 3;
	REQUIRE(ak_op_divn(R, 0, 1, 2, AK_U64, false));
	CHECK((uint64_t)R[0].i == UINT64_MAX / 3);
	R[2].i = 10;
	REQUIRE(ak_op_divn(R, 0, 1, 2, AK_U64, true));
	CHECK(R[0].i == 5);
	R[2].i = 0;
	check_error(ak_op_divn(R, 0, 1, 2, AK_U64, false), "division by zero");
	R[1].i = INT64_C(-9223372036854775807) - 1, R[2].i = 1;  // 2^63, a u64
	REQUIRE(ak_op_addn(R, 0, 1, 2, AK_U64));
	CHECK((uint64_t)R[0].i == UINT64_C(9223372036854775809));
	REQUIRE(!ak_error_pending());
}

TEST(shifts_and_powers) {
	ak_val R[3];
	// u8 << drops what's past 8 bits; >> of a u64 is logical, of an i8
	// arithmetic.
	R[1].i = 200, R[2].i = 1;
	REQUIRE(ak_op_shiftn(R, 0, 1, 2, AK_U8 | AK_I64 << 4, false));
	CHECK(R[0].i == 144);
	R[1].i = -1, R[2].i = 60;
	REQUIRE(ak_op_shiftn(R, 0, 1, 2, AK_U64 | AK_I64 << 4, true));
	CHECK(R[0].i == 15);
	R[1].i = -64, R[2].i = 2;
	REQUIRE(ak_op_shiftn(R, 0, 1, 2, AK_I8 | AK_U8 << 4, true));
	CHECK(R[0].i == -16);
	R[2].i = -1;
	check_error(ak_op_shiftn(R, 0, 1, 2, AK_I8 | AK_I64 << 4, true), "negative shift amount");
	R[2].i = -1;  // a u64's count past int's largest: every bit out
	REQUIRE(ak_op_shiftn(R, 0, 1, 2, AK_I8 | AK_U64 << 4, true));
	CHECK(R[0].i == -1);
	// Powers.
	int64_t r;
	CHECK(ak_int_pow(2, 62, AK_I64, &r) && r == INT64_C(4611686018427387904));
	CHECK(!ak_int_pow(2, 63, AK_I64, &r));
	CHECK(ak_int_pow(-2, 63, AK_I64, &r) && r == INT64_MIN);
	CHECK(ak_int_pow(2, 63, AK_U64, &r) && (uint64_t)r == UINT64_C(9223372036854775808));
	CHECK(!ak_int_pow(2, 8, AK_U8, &r) && ak_int_pow(2, 7, AK_U8, &r) && r == 128);
	CHECK(ak_int_pow(-1, UINT64_MAX, AK_I64, &r) && r == -1);
	CHECK(ak_int_pow(0, 0, AK_I64, &r) && r == 1);
	CHECK(ak_int_pow(3, 39, AK_I64, &r) && r == INT64_C(4052555153018976267));
	CHECK(!ak_int_pow(3, 40, AK_I64, &r));
	CHECK(ak_int_pow(3, 40, AK_U64, &r) && (uint64_t)r == UINT64_C(12157665459056928801));
	R[1].i = 2, R[2].i = -1;
	check_error(ak_op_pown(R, 0, 1, 2, AK_I64 | AK_I64 << 4), "a negative exponent: an integer's negative power isn't an integer");
	REQUIRE(!ak_error_pending());
}

TEST(conversions) {
	ak_val R[2];
	R[1].i = 300;
	R[0].i = 9;
	CHECK(!ak_op_convn(R, 0, 1, AK_U8 | AK_I64 << 4));
	void *e = ak_error_take();
	REQUIRE(e != NULL);
	CHECK_EQ_STR(ak_error_message(e)->data, "integer overflow: 300 doesn't fit in a u8");
	ak_release(e);
	CHECK(R[0].i == 9);
	R[1].i = -1;
	CHECK(!ak_op_convn(R, 0, 1, AK_I64 | AK_U64 << 4));
	e = ak_error_take();
	REQUIRE(e != NULL);
	CHECK_EQ_STR(ak_error_message(e)->data, "integer overflow: 18446744073709551615 doesn't fit in an int");
	ak_release(e);
	REQUIRE(ak_op_convn(R, 0, 1, AK_U64 | AK_U64 << 4));
	CHECK(ak_op_f2n(3.9, AK_U8) == 3 && ak_op_f2n(-3.9, AK_U8) == 0 && ak_op_f2n(1000, AK_U8) == 255);
	CHECK(ak_op_f2n(0.0 / 0.0, AK_I32) == 0 && ak_op_f2n(-1e300, AK_I16) == -32768);
	CHECK(ak_op_f2n(1e30, AK_U64) == -1 && (uint64_t)ak_op_f2n(1.8446744073709550e19, AK_U64) == UINT64_C(18446744073709549568));
}

TEST(text) {
	char buf[66];
	int n = ak_int_text(buf, -1, AK_U64, 10);
	CHECK(n == 20 && memcmp(buf, "18446744073709551615", 20) == 0);
	n = ak_int_text(buf, -1, AK_U64, 16);
	CHECK(n == 16 && memcmp(buf, "ffffffffffffffff", 16) == 0);
	n = ak_int_text(buf, INT64_MIN, AK_I64, 2);
	CHECK(n == 65 && buf[0] == '-' && buf[1] == '1');
	int64_t v;
	CHECK(ak_int_parse("18446744073709551615", 20, 10, AK_U64, &v) && v == -1);
	CHECK(!ak_int_parse("18446744073709551616", 20, 10, AK_U64, &v));
	CHECK(!ak_int_parse("-1", 2, 10, AK_U64, &v) && ak_int_parse("-0", 2, 10, AK_U64, &v) && v == 0);
	CHECK(ak_int_parse("-9223372036854775808", 20, 10, AK_I64, &v) && v == INT64_MIN);
	CHECK(!ak_int_parse("9223372036854775808", 19, 10, AK_I64, &v));
	CHECK(ak_int_parse("-80", 3, 16, AK_I8, &v) && v == -128 && !ak_int_parse("80", 2, 16, AK_I8, &v));
	CHECK(ak_int_parse("FF", 2, 16, AK_U8, &v) && v == 255 && !ak_int_parse("", 0, 10, AK_U8, &v) && !ak_int_parse("+", 1, 10, AK_U8, &v));
	ak_str *s = ak_op_tostr((ak_val){.i = -2}, AK_K_U64TEXT);
	CHECK_EQ_STR(s->data, "18446744073709551614");
	ak_release(s);
}

// The N instructions allocate nothing.
TEST(sized_ops_allocate_nothing) {
	ak_val R[4] = {{.i = 0}, {.i = 0}, {.i = 3}, {.i = 0}};
	uint64_t before = ak_alloc_count;
	for (int i = 0; i < 100000; i++) {
		REQUIRE(ak_op_addn(R, 0, 2, 2, AK_U16));
		REQUIRE(ak_op_muln(R, 1, 0, 2, AK_I32));
		REQUIRE(ak_op_subn(R, 3, 1, 0, AK_I32));
		REQUIRE(ak_op_divn(R, 3, 3, 2, AK_U64, false));
		REQUIRE(ak_op_shiftn(R, 1, 1, 2, AK_U8 | AK_I64 << 4, false));
		REQUIRE(ak_op_pown(R, 3, 2, 2, AK_U32 | AK_I64 << 4));
		REQUIRE(ak_op_convn(R, 1, 3, AK_U8 | AK_U32 << 4));
	}
	CHECK_EQ_INT(ak_alloc_count - before, 0);
	CHECK(R[3].i == 27);
}

T_MAIN();
