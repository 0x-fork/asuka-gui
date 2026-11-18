# Mbed TLS

A subset of Mbed TLS 4.1.2 (https://github.com/Mbed-TLS/mbedtls,
`mbedtls-4.1.2.tar.bz2`, SHA-256 392215e5e4846df04e2f4c4516079303bd73edde837d8f3e68fb4d4d7ddf0126),
the Mbed TLS project's, Apache-2.0 licensed (LICENSE; or GPL-2.0-or-later
at the user's choice): TLS 1.2 and 1.3 and X.509 for std/net's TLS
(core/src/std/tls.c), and TF-PSA-Crypto, the crypto it's built on. Mbed
TLS 4.1 is a long-term support branch, fixed until at least March 2029.

Written by internal/mbedtlsgen (`go generate ./internal/mbedtlsgen`), which
says how: the release's public headers under include/, the C files
below and the internal headers they include flat in src/, all
unchanged but the configuration (include/mbedtls/mbedtls_config.h,
include/psa/crypto_config.h, include/mbedtls/threading_alt.h: Asuka's,
from internal/mbedtlsgen). Each C file has a file in
core/src/std/mbedtls that includes it, after names.h, which renames
what it exports to the core's names (ak_mbedtls_...). The link leaves
out what nothing calls, and nothing does unless an app calls std/net's
TLS (core/link/link.c's AK_USES_TLS).

The C files:

- `ssl_ciphersuites.c`
- `ssl_client.c`
- `ssl_msg.c`
- `ssl_tls.c`
- `ssl_tls12_client.c`
- `ssl_tls12_server.c`
- `ssl_tls13_client.c`
- `ssl_tls13_generic.c`
- `ssl_tls13_keys.c`
- `ssl_tls13_server.c`
- `mps_reader.c`
- `x509.c`
- `x509_crt.c`
- `x509_oid.c`
- `psa_crypto.c`
- `psa_crypto_client.c`
- `psa_crypto_driver_wrappers_no_static.c`
- `psa_crypto_slot_management.c`
- `psa_util.c`
- `aes.c`
- `aesce.c`
- `aesni.c`
- `block_cipher.c`
- `gcm.c`
- `chacha20.c`
- `chacha20_neon.c`
- `chachapoly.c`
- `poly1305.c`
- `sha256.c`
- `sha512.c`
- `bignum.c`
- `bignum_core.c`
- `ecp.c`
- `ecp_curves.c`
- `ecdsa.c`
- `rsa.c`
- `rsa_alt_helpers.c`
- `psa_crypto_aead.c`
- `psa_crypto_cipher.c`
- `psa_crypto_ecp.c`
- `psa_crypto_hash.c`
- `psa_crypto_mac.c`
- `psa_crypto_rsa.c`
- `psa_util_internal.c`
- `platform.c`
- `platform_util.c`
- `threading.c`
- `asn1parse.c`
- `asn1write.c`
- `base64.c`
- `constant_time.c`
- `oid.c`
- `pem.c`
- `md.c`
- `pk.c`
- `pk_ecc.c`
- `pk_rsa.c`
- `pk_wrap.c`
- `pkparse.c`
- `pkwrite.c`

To update: change the version and SHA-256 in internal/mbedtlsgen and run
it; to add a file, add it to its list.
