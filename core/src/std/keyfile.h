// keyfile.h: private keys as files hold them (keyfile.c), encrypted or
// not: what std/crypto's privateKey and decryptPrivateKey read, and what
// PrivateKey.encryptedPEM(password:) writes.
#ifndef AK_KEYFILE_H
#define AK_KEYFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The form of a key ak_key_read read: its DER is PKCS#8's PrivateKeyInfo,
// PKCS#1's RSAPrivateKey or SEC 1's ECPrivateKey.
enum { AK_KEY_PKCS8 = 1, AK_KEY_PKCS1, AK_KEY_SEC1 };
// Why it read none: the key isn't one it reads, the password doesn't
// decrypt it (a wrong one, or none for a key that's encrypted), or it's
// encrypted and ak_key_read was asked.
enum { AK_KEY_INVALID = -1, AK_KEY_PASSWORD = -2, AK_KEY_ENCRYPTED = -3 };

// ak_key_read reads the private key in p[0:n], not encrypted: PEM (pem
// true: the first of its blocks that's a private key's, the others
// skipped) or DER. PEM's PRIVATE KEY, RSA PRIVATE KEY, EC PRIVATE KEY and
// OPENSSH PRIVATE KEY. It returns the key's form, its DER in *der (*dn
// bytes, ak_alloc'd: the caller wipes and frees it), or why it read none
// (AK_KEY_INVALID; AK_KEY_ENCRYPTED for one that's encrypted) with why in
// why. It links none of the ciphers ak_key_decrypt takes.
int ak_key_read(const uint8_t *p, size_t n, bool pem, uint8_t **der, size_t *dn, char *why, size_t cap);

// ak_key_decrypt is ak_key_read, and encrypted keys decrypted with the
// password pw[0:pn]: ENCRYPTED PRIVATE KEY (PKCS#8's PBES2, PKCS#12's
// Triple DES), OpenSSL's traditional PEM (Proc-Type: 4,ENCRYPTED),
// OpenSSH's (bcrypt_pbkdf). AK_KEY_PASSWORD when the password doesn't
// decrypt it. It takes as long as the key's key derivation does, on
// purpose: the caller's thread (spawn's, std/crypto's).
int ak_key_decrypt(const uint8_t *p, size_t n, bool pem, const uint8_t *pw, size_t pn, uint8_t **der, size_t *dn, char *why, size_t cap);

// ak_key_encrypt is PKCS#8 key der[0:n] encrypted with pw[0:pn] as OpenSSL
// 3 writes it: EncryptedPrivateKeyInfo, PBES2 of PBKDF2-HMAC-SHA256
// (iterations, a random 16-byte salt) and AES-256-CBC (a random IV). Its
// DER into *out (*on bytes, ak_alloc'd).
void ak_key_encrypt(const uint8_t *der, size_t n, const uint8_t *pw, size_t pn, uint32_t iterations, uint8_t **out, size_t *on);

// ak_bcrypt_pbkdf is OpenBSD's bcrypt_pbkdf (password.c, over
// crypt_blowfish's Blowfish): kn bytes of key from a password and a salt,
// rounds of it (OpenSSH's key files: ssh-keygen -a).
void ak_bcrypt_pbkdf(const uint8_t *pw, size_t pn, const uint8_t *salt, size_t sn, uint32_t rounds, uint8_t *key, size_t kn);

#endif
