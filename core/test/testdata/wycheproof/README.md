# Wycheproof

Subsets of Project Wycheproof's test vectors
(https://github.com/C2SP/wycheproof, `testvectors_v1` at commit
65ca494727a52944c204441c860b039e6786b99b, Apache License 2.0), which
core/test/aead_test.c and core/test/pubkey_test.c check std/crypto's C
against: AES-GCM (12-byte nonces, 128- and 256-bit keys),
ChaCha20-Poly1305, XChaCha20-Poly1305, ECDSA over P-256 (r ‖ s and DER),
P-384 and P-521, RSA PKCS#1 v1.5 and PSS signatures. convert.go wrote
them from the JSON files (what it keeps and the lines' format are in its
comment); to update, run it on a newer checkout and change the commit
here.
