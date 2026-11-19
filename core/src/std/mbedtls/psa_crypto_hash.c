// Mbed TLS's psa_crypto_hash.c, a file of its own as Mbed TLS compiles it (its files
// share macro and static names): third_party/mbedtls/README.md. Not the
// web's, which has no sockets.
#if !defined(__wasm__)
#include "std/mbedtls/names.h"
#include "../../../third_party/mbedtls/src/psa_crypto_hash.c"
#endif
