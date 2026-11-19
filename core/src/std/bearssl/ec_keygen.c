// BearSSL's ec_keygen.c, a file of its own as BearSSL compiles it (its files
// share macro and static names): third_party/bearssl/README.md.
#include "std/bearssl/names.h"
// It calls implementations through BearSSL's vtables, whose functions
// take the class pointer as their own context type: clang's
// -fsanitize=function (UBSan's, since clang 17) reports each such call
// as a mismatch, so it's off for this file's functions.
#if defined(__clang__)
#pragma clang attribute push(__attribute__((no_sanitize("function"))), apply_to = function)
#endif
#include "../../../third_party/bearssl/ec_keygen.c"
#if defined(__clang__)
#pragma clang attribute pop
#endif
