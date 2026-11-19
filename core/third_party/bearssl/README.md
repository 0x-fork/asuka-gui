# BearSSL

A subset of BearSSL 0.6 (https://bearssl.org/, `bearssl-0.6.tar.gz`,
SHA-256 6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14),
Thomas Pornin's, MIT licensed (LICENSE.txt): its constant-time
implementations of what std/crypto needs, the files unchanged but laid
out flat (BearSSL's `inc/*.h`, `src/inner.h`, `src/config.h`, and the
`.c` files below from its `src/` directories):

- AES-GCM (aead.c): `aes_ct64.c`, `aes_ct64_enc.c`, `aes_ct64_ctr.c`
  (AES bitsliced in 64-bit words), `ghash_ctmul64.c`, `gcm.c`.
- RSA (pubkey.c): `rsa_i31_pub.c`, `rsa_i31_priv.c` and the `i31_*.c`
  big integers (31-bit words, constant time).
- ECDSA over P-256, P-384 and P-521 (pubkey.c): `ec_prime_i31.c`,
  `ec_secp256r1.c`, `ec_secp384r1.c`, `ec_secp521r1.c`, `ec_pubkey.c`,
  `ec_keygen.c`, `ecdsa_i31_*.c`, `ecdsa_rta.c`; signing
  is deterministic (RFC 6979) over `hmac_drbg.c`, `hmac.c`,
  `sha2small.c` and `sha2big.c`.
- Private keys' encryption (keyfile.c: PKCS#8's PBES2, OpenSSL's and
  OpenSSH's key files): `aes_ct64_dec.c`, `aes_ct64_cbcdec.c`,
  `aes_ct64_cbcenc.c` (AES-CBC); `des_ct.c`, `des_support.c`,
  `des_ct_cbcdec.c` (Triple DES's CBC, for older keys, decrypted only).
- What those share: `ccopy.c`, `dec*.c`, `enc*.c`, `i32_div32.c`.

BearSSL compiles each file on its own (they share macro and static
names), so each has a file in core/src/std/bearssl that includes it,
first including core/src/std/bearssl/names.h, which renames BearSSL's
exported names to the core's (`ak_br_*`); the C that calls BearSSL
includes core/src/std/bearssl/bearssl.h, which does the same. The link
leaves out what nothing calls. To update: replace the files with a newer
release's, regenerate names.h from what the objects define, and change
the version here. To add one of BearSSL's files: copy it here, add its
file to core/src/std/bearssl, and add its names to names.h.
