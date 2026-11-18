// mbedtls/threading_alt.h: Mbed TLS's locks, the core's (base/thread.h's
// ak_mutex and ak_cond, made by std/tls.c), behind a pointer so
// this header needs no system's.
#ifndef AK_MBEDTLS_THREADING_ALT_H
#define AK_MBEDTLS_THREADING_ALT_H
typedef struct {
	void *p;
} mbedtls_platform_mutex_t;
typedef struct {
	void *p;
} mbedtls_platform_condition_variable_t;
#endif
