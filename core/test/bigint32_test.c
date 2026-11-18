// std/bigint's builtins on 32-bit limbs, as wasm32, armv7 and i686 run
// them, whatever this computer's are: bigint.c compiled again here with
// AK_BIGINT_LIMB32, its names apart from the core's, and bigint_cases.h
// run on it.
#define AK_BIGINT_LIMB32 1
#define ak_std_bigint ak_std_bigint32
#define ak_bigint_is ak_bigint32_is
#define ak_bigint_write ak_bigint32_write
#define ak_bigint_read ak_bigint32_read
#include "std/bigint.c"

#define BI_TABLE ak_std_bigint32
#include "bigint_cases.h"

T_MAIN();
