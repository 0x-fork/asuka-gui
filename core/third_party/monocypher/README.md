# Monocypher

Monocypher 4.0.2 (tag `4.0.2`, https://github.com/LoupVaillant/Monocypher,
the tag's source archive, SHA-256
bc1ca30b1b2654e4e7daf2492c0d204200e55137f23fda6b7142fd7d523bd6b4),
dual-licensed BSD-2-Clause or CC0-1.0 (LICENCE.md): `src/monocypher.c`
and `src/monocypher.h`, unchanged. core/src/std/monocypher.c compiles them
in, their exported names renamed to the core's (`ak_mc_*`,
core/src/std/monocypher.h), for std/crypto's ChaCha20-Poly1305 and
XChaCha20-Poly1305 (RFC 8439; aead.c) and Argon2 (RFC 9106, over its
BLAKE2b; password.c). What std/crypto doesn't call (X25519, EdDSA,
Elligator) the link leaves out. To update: replace the two files with a
newer release's, regenerate the names in monocypher.h from what the
object defines, and change the version here.
