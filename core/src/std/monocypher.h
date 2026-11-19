// monocypher.h: Monocypher's API (third_party/monocypher), its exported
// names the core's own (ak_mc_...): an
// app's C or a library it links that has Monocypher (or another
// crypto_ library) of its own doesn't clash with it. std/crypto's
// ChaCha20-Poly1305 (aead.c) and Argon2 (password.c) call it.
#ifndef AK_MONOCYPHER_H
#define AK_MONOCYPHER_H
#define crypto_aead_init_djb ak_mc_aead_init_djb
#define crypto_aead_init_ietf ak_mc_aead_init_ietf
#define crypto_aead_init_x ak_mc_aead_init_x
#define crypto_aead_lock ak_mc_aead_lock
#define crypto_aead_read ak_mc_aead_read
#define crypto_aead_unlock ak_mc_aead_unlock
#define crypto_aead_write ak_mc_aead_write
#define crypto_argon2 ak_mc_argon2
#define crypto_argon2_no_extras ak_mc_argon2_no_extras
#define crypto_blake2b ak_mc_blake2b
#define crypto_blake2b_final ak_mc_blake2b_final
#define crypto_blake2b_init ak_mc_blake2b_init
#define crypto_blake2b_keyed ak_mc_blake2b_keyed
#define crypto_blake2b_keyed_init ak_mc_blake2b_keyed_init
#define crypto_blake2b_update ak_mc_blake2b_update
#define crypto_chacha20_djb ak_mc_chacha20_djb
#define crypto_chacha20_h ak_mc_chacha20_h
#define crypto_chacha20_ietf ak_mc_chacha20_ietf
#define crypto_chacha20_x ak_mc_chacha20_x
#define crypto_eddsa_check ak_mc_eddsa_check
#define crypto_eddsa_check_equation ak_mc_eddsa_check_equation
#define crypto_eddsa_key_pair ak_mc_eddsa_key_pair
#define crypto_eddsa_mul_add ak_mc_eddsa_mul_add
#define crypto_eddsa_reduce ak_mc_eddsa_reduce
#define crypto_eddsa_scalarbase ak_mc_eddsa_scalarbase
#define crypto_eddsa_sign ak_mc_eddsa_sign
#define crypto_eddsa_to_x25519 ak_mc_eddsa_to_x25519
#define crypto_eddsa_trim_scalar ak_mc_eddsa_trim_scalar
#define crypto_elligator_key_pair ak_mc_elligator_key_pair
#define crypto_elligator_map ak_mc_elligator_map
#define crypto_elligator_rev ak_mc_elligator_rev
#define crypto_poly1305 ak_mc_poly1305
#define crypto_poly1305_final ak_mc_poly1305_final
#define crypto_poly1305_init ak_mc_poly1305_init
#define crypto_poly1305_update ak_mc_poly1305_update
#define crypto_verify16 ak_mc_verify16
#define crypto_verify32 ak_mc_verify32
#define crypto_verify64 ak_mc_verify64
#define crypto_wipe ak_mc_wipe
#define crypto_x25519 ak_mc_x25519
#define crypto_x25519_dirty_fast ak_mc_x25519_dirty_fast
#define crypto_x25519_dirty_small ak_mc_x25519_dirty_small
#define crypto_x25519_inverse ak_mc_x25519_inverse
#define crypto_x25519_public_key ak_mc_x25519_public_key
#define crypto_x25519_to_eddsa ak_mc_x25519_to_eddsa
#include "../../third_party/monocypher/monocypher.h"
#endif
