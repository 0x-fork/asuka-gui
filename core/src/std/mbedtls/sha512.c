// Mbed TLS's sha512.c, a file of its own as Mbed TLS compiles it (its files
// share macro and static names): third_party/mbedtls/README.md. Not the
// web's, which has no sockets.
#if !defined(__wasm__)
#include "std/mbedtls/names.h"
#include "../../../third_party/mbedtls/src/sha512.c"
#endif
