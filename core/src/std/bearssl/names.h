// names.h: BearSSL's exported names, the core's own (ak_br_...), so an
// app's C or a library it links that has BearSSL of its own doesn't
// clash with the subset the core compiles in (third_party/bearssl).
// Every file of core/src/std/bearssl includes it first, and so does
// what calls BearSSL (bearssl.h here). The list is what the subset's
// objects define (nm); a file added to the subset adds its names.
#ifndef AK_BEARSSL_NAMES_H
#define AK_BEARSSL_NAMES_H
#define br_aes_ct64_bitslice_Sbox ak_br_aes_ct64_bitslice_Sbox
#define br_aes_ct64_bitslice_encrypt ak_br_aes_ct64_bitslice_encrypt
#define br_aes_ct64_ctr_init ak_br_aes_ct64_ctr_init
#define br_aes_ct64_ctr_run ak_br_aes_ct64_ctr_run
#define br_aes_ct64_ctr_vtable ak_br_aes_ct64_ctr_vtable
#define br_aes_ct64_interleave_in ak_br_aes_ct64_interleave_in
#define br_aes_ct64_interleave_out ak_br_aes_ct64_interleave_out
#define br_aes_ct64_keysched ak_br_aes_ct64_keysched
#define br_aes_ct64_ortho ak_br_aes_ct64_ortho
#define br_aes_ct64_skey_expand ak_br_aes_ct64_skey_expand
#define br_ccopy ak_br_ccopy
#define br_divrem ak_br_divrem
#define br_ec_compute_pub ak_br_ec_compute_pub
#define br_ec_keygen ak_br_ec_keygen
#define br_ec_prime_i31 ak_br_ec_prime_i31
#define br_ecdsa_i31_bits2int ak_br_ecdsa_i31_bits2int
#define br_ecdsa_i31_sign_raw ak_br_ecdsa_i31_sign_raw
#define br_ecdsa_i31_vrfy_raw ak_br_ecdsa_i31_vrfy_raw
#define br_ecdsa_raw_to_asn1 ak_br_ecdsa_raw_to_asn1
#define br_gcm_aad_inject ak_br_gcm_aad_inject
#define br_gcm_check_tag ak_br_gcm_check_tag
#define br_gcm_check_tag_trunc ak_br_gcm_check_tag_trunc
#define br_gcm_flip ak_br_gcm_flip
#define br_gcm_get_tag ak_br_gcm_get_tag
#define br_gcm_get_tag_trunc ak_br_gcm_get_tag_trunc
#define br_gcm_init ak_br_gcm_init
#define br_gcm_reset ak_br_gcm_reset
#define br_gcm_run ak_br_gcm_run
#define br_gcm_vtable ak_br_gcm_vtable
#define br_ghash_ctmul64 ak_br_ghash_ctmul64
#define br_hmac_drbg_generate ak_br_hmac_drbg_generate
#define br_hmac_drbg_init ak_br_hmac_drbg_init
#define br_hmac_drbg_update ak_br_hmac_drbg_update
#define br_hmac_drbg_vtable ak_br_hmac_drbg_vtable
#define br_hmac_init ak_br_hmac_init
#define br_hmac_key_init ak_br_hmac_key_init
#define br_hmac_out ak_br_hmac_out
#define br_hmac_update ak_br_hmac_update
#define br_i31_add ak_br_i31_add
#define br_i31_bit_length ak_br_i31_bit_length
#define br_i31_decode ak_br_i31_decode
#define br_i31_decode_mod ak_br_i31_decode_mod
#define br_i31_decode_reduce ak_br_i31_decode_reduce
#define br_i31_encode ak_br_i31_encode
#define br_i31_from_monty ak_br_i31_from_monty
#define br_i31_iszero ak_br_i31_iszero
#define br_i31_moddiv ak_br_i31_moddiv
#define br_i31_modpow ak_br_i31_modpow
#define br_i31_modpow_opt ak_br_i31_modpow_opt
#define br_i31_montymul ak_br_i31_montymul
#define br_i31_mulacc ak_br_i31_mulacc
#define br_i31_muladd_small ak_br_i31_muladd_small
#define br_i31_ninv31 ak_br_i31_ninv31
#define br_i31_reduce ak_br_i31_reduce
#define br_i31_rshift ak_br_i31_rshift
#define br_i31_sub ak_br_i31_sub
#define br_i31_to_monty ak_br_i31_to_monty
#define br_range_dec32be ak_br_range_dec32be
#define br_range_dec32le ak_br_range_dec32le
#define br_range_dec64be ak_br_range_dec64be
#define br_range_enc32be ak_br_range_enc32be
#define br_range_enc32le ak_br_range_enc32le
#define br_range_enc64be ak_br_range_enc64be
#define br_rsa_i31_private ak_br_rsa_i31_private
#define br_rsa_i31_public ak_br_rsa_i31_public
#define br_secp256r1 ak_br_secp256r1
#define br_secp384r1 ak_br_secp384r1
#define br_secp521r1 ak_br_secp521r1
#define br_sha224_IV ak_br_sha224_IV
#define br_sha224_init ak_br_sha224_init
#define br_sha224_out ak_br_sha224_out
#define br_sha224_set_state ak_br_sha224_set_state
#define br_sha224_state ak_br_sha224_state
#define br_sha224_update ak_br_sha224_update
#define br_sha224_vtable ak_br_sha224_vtable
#define br_sha256_IV ak_br_sha256_IV
#define br_sha256_init ak_br_sha256_init
#define br_sha256_out ak_br_sha256_out
#define br_sha256_vtable ak_br_sha256_vtable
#define br_sha2small_round ak_br_sha2small_round
#define br_sha384_init ak_br_sha384_init
#define br_sha384_out ak_br_sha384_out
#define br_sha384_set_state ak_br_sha384_set_state
#define br_sha384_state ak_br_sha384_state
#define br_sha384_update ak_br_sha384_update
#define br_sha384_vtable ak_br_sha384_vtable
#define br_sha512_init ak_br_sha512_init
#define br_sha512_out ak_br_sha512_out
#define br_sha512_vtable ak_br_sha512_vtable
#endif
