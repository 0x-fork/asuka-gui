# crypt_blowfish

crypt_blowfish 1.3 (https://www.openwall.com/crypt/, `crypt_blowfish-1.3.tar.gz`,
SHA-256 83fa01fca6996fe8d882b7f8e9ba0305a5664936100b01481ea3c6a8ce8d72fd),
Solar Designer's bcrypt, the one PHP and many systems' crypt(3) use:
public domain, or where that isn't possible, "redistribution and use in
source and binary forms, with or without modification, are permitted"
(the notice at the top of crypt_blowfish.c). `crypt_blowfish.c` and
`crypt_blowfish.h`, unchanged but for one line: its C runs on i386 too
(BF_ASM 0 there), as its x86 assembly (x86.S) isn't vendored.
core/src/std/password.c compiles them in (std/crypto's bcrypt: `$2a$`,
`$2b$` and `$2y$` hashes), their exported names renamed to the core's
(`ak_bf_*`). To update: replace the two files with a newer release's,
make the same change, and change the version here.
