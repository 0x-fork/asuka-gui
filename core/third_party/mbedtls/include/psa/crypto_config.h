// psa/crypto_config.h: Asuka's TF-PSA-Crypto configuration (in place of
// the release's): what TLS 1.2 and 1.3 need, for std/net's TLS and
// nothing else. Written by internal/mbedtlsgen; edit it there.
//
// Key exchange: ECDHE over X25519, P-256, P-384 and P-521. Signatures:
// RSA (PKCS#1 v1.5, PSS) and ECDSA. Ciphers: AES-GCM (AES-NI and the
// ARMv8 crypto extensions when the processor has them) and
// ChaCha20-Poly1305. Hashes: SHA-256, SHA-384, SHA-512. Randomness: the
// system's (ak_os_random), through MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG.
#ifndef PSA_CRYPTO_CONFIG_H
#define PSA_CRYPTO_CONFIG_H

#define TF_PSA_CRYPTO_CONFIG_VERSION 0x01000000

#define PSA_WANT_ALG_CHACHA20_POLY1305 1
#define PSA_WANT_ALG_ECDH 1
#define PSA_WANT_ALG_ECDSA 1
#define PSA_WANT_ALG_GCM 1
#define PSA_WANT_ALG_HKDF 1
#define PSA_WANT_ALG_HKDF_EXTRACT 1
#define PSA_WANT_ALG_HKDF_EXPAND 1
#define PSA_WANT_ALG_HMAC 1
#define PSA_WANT_ALG_RSA_PKCS1V15_SIGN 1
#define PSA_WANT_ALG_RSA_PSS 1
#define PSA_WANT_ALG_SHA_256 1
#define PSA_WANT_ALG_SHA_384 1
#define PSA_WANT_ALG_SHA_512 1
#define PSA_WANT_ALG_TLS12_PRF 1

#define PSA_WANT_ECC_MONTGOMERY_255 1
#define PSA_WANT_ECC_SECP_R1_256 1
#define PSA_WANT_ECC_SECP_R1_384 1
#define PSA_WANT_ECC_SECP_R1_521 1

#define PSA_WANT_KEY_TYPE_DERIVE 1
#define PSA_WANT_KEY_TYPE_HMAC 1
#define PSA_WANT_KEY_TYPE_AES 1
#define PSA_WANT_KEY_TYPE_CHACHA20 1
#define PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY 1
#define PSA_WANT_KEY_TYPE_RAW_DATA 1
#define PSA_WANT_KEY_TYPE_RSA_PUBLIC_KEY 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_IMPORT 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_EXPORT 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE 1
#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_BASIC 1
#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_IMPORT 1
#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_EXPORT 1

#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE
#define MBEDTLS_PLATFORM_C

// The PSA store, the RNG and the key slots are shared by the threads
// that speak TLS (std/net's, each HTTP server's): their locks are the
// core's (std/tls.c).
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT

#define MBEDTLS_MD_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_WRITE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

#define MBEDTLS_PSA_CRYPTO_C
#define MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG
#define MBEDTLS_PSA_KEY_STORE_DYNAMIC

#define MBEDTLS_AESNI_C
#define MBEDTLS_AESCE_C
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_HAVE_ASM

#endif
