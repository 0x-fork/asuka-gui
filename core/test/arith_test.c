// The VM's int instructions (vm/ops.h: the interpreter's and compiled
// C's): +, -, * and -x checked, throwing "integer overflow" past int's
// range, as / throws for zero; nothing allocated on the way that fits.
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

TEST(checked_ops) {
	ak_val R[3] = {{.i = 0}, {.i = INT64_MAX}, {.i = 1}};
	check_overflow(ak_op_addi(R, 0, 1, 2));
	CHECK(R[0].i == 0); // the destination is left as it was
	check_overflow(ak_op_muli(R, 0, 1, 1));
	R[1].i = INT64_MIN;
	check_overflow(ak_op_subi(R, 0, 1, 2));
	check_overflow(ak_op_negi(R, 0, 1));
	R[2].i = -1;
	check_overflow(ak_op_muli(R, 0, 1, 2));
	check_overflow(ak_op_divi(R, 0, 1, 2, false));
	REQUIRE(ak_op_divi(R, 0, 1, 2, true));
	CHECK(R[0].i == 0);
	REQUIRE(!ak_error_pending());

	// At the edges, and past them on the other side.
	R[1].i = INT64_MAX - 1, R[2].i = 1;
	REQUIRE(ak_op_addi(R, 0, 1, 2));
	CHECK(R[0].i == INT64_MAX);
	R[1].i = INT64_MIN + 1;
	REQUIRE(ak_op_subi(R, 0, 1, 2));
	CHECK(R[0].i == INT64_MIN);
	REQUIRE(ak_op_negi(R, 0, 2));
	CHECK(R[0].i == -1);
	R[1].i = INT64_C(3037000499), R[2].i = INT64_C(3037000499);
	REQUIRE(ak_op_muli(R, 0, 1, 2));
	CHECK(R[0].i == INT64_C(9223372030926249001));
	R[1].i = INT64_MIN / 2, R[2].i = 2;
	REQUIRE(ak_op_muli(R, 0, 1, 2));
	CHECK(R[0].i == INT64_MIN);
	REQUIRE(!ak_error_pending());
}

// The checks allocate nothing: a loop of them is as free as before.
TEST(checked_ops_allocate_nothing) {
	ak_val R[4] = {{.i = 0}, {.i = 0}, {.i = 3}, {.i = 0}};
	uint64_t before = ak_alloc_count;
	for (int i = 0; i < 100000; i++) {
		REQUIRE(ak_op_addi(R, 0, 0, 2));
		REQUIRE(ak_op_muli(R, 1, 0, 2));
		REQUIRE(ak_op_subi(R, 1, 1, 0));
		REQUIRE(ak_op_negi(R, 3, 1));
	}
	CHECK_EQ_INT(ak_alloc_count - before, 0);
	CHECK(R[0].i == 300000);
	CHECK(R[3].i == -600000);
}

T_MAIN();
