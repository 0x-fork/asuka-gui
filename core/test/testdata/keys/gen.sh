#!/bin/sh
# gen.sh writes keyfile_test.c's private keys, encrypted as the real tools
# encrypt them: OpenSSL 3 (OPENSSL, default openssl) and OpenSSH's
# ssh-keygen. Every key's password is "asuka", but pkcs12-unicode.pem's,
# which is "pässwörd🔑" (UTF-8). Run it here: sh gen.sh. tls-*.pem are
# ../tls's server.key and client.key (gen.go's), encrypted: run it again
# after gen.go.
set -eu
O=${OPENSSL:-openssl}
P=pass:asuka
rm -f *.pem *.der *.pub ssh-*

# The keys, plain (PKCS#8).
$O genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out rsa.pem
$O genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out p256.pem
$O genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-384 -out p384.pem
$O genpkey -algorithm ED25519 -out ed25519.pem
# Their public keys, to check what's read against.
for k in rsa p256 p384 ed25519; do $O pkey -in $k.pem -pubout -outform DER -out $k-public.der; done

# PKCS#8 encrypted: PBES2 as OpenSSL 3 writes it by default (PBKDF2 of
# HMAC-SHA-256, 2048 iterations; AES-256-CBC), then its other choices.
for k in rsa p256 ed25519; do $O pkcs8 -topk8 -in $k.pem -passout $P -out $k-pbes2.pem; done
$O pkcs8 -topk8 -in p256.pem -passout $P -v2 aes-128-cbc -v2prf hmacWithSHA1 -out p256-aes128-sha1.pem
$O pkcs8 -topk8 -in p384.pem -passout $P -v2 aes-192-cbc -v2prf hmacWithSHA512 -iter 10000 -out p384-aes192-sha512.pem
$O pkcs8 -topk8 -in p256.pem -passout $P -v2 des3 -v2prf hmacWithSHA384 -out p256-des3.pem
$O pkcs8 -topk8 -in p256.pem -passout $P -scrypt -out p256-scrypt.pem
$O pkcs8 -topk8 -in p256.pem -passout $P -outform DER -out p256-pbes2.der
# PKCS#12's Triple DES (OpenSSL 1.0's default), its two-key one, and a
# password that isn't ASCII (PKCS#12 takes it as UTF-16).
$O pkcs8 -topk8 -in rsa.pem -passout $P -v1 PBE-SHA1-3DES -out rsa-pkcs12-3des.pem
$O pkcs8 -topk8 -in p256.pem -passout $P -v1 PBE-SHA1-2DES -out p256-pkcs12-2des.pem
$O pkcs8 -topk8 -in p256.pem -passout 'pass:pässwörd🔑' -v1 PBE-SHA1-3DES -out pkcs12-unicode.pem
# Weak ones, refused: PBES1's single DES (OpenSSL's legacy provider).
$O pkcs8 -topk8 -in p256.pem -passout $P -v1 PBE-SHA1-DES -provider legacy -provider default -out p256-pbes1-des.pem

# Traditional PEM (Proc-Type: 4,ENCRYPTED; DEK-Info).
$O pkey -in rsa.pem -traditional -aes256 -passout $P -out rsa-trad-aes256.pem
$O pkey -in rsa.pem -traditional -des3 -passout $P -out rsa-trad-des3.pem
$O pkey -in p256.pem -traditional -aes128 -passout $P -out p256-trad-aes128.pem
$O pkey -in p256.pem -traditional -out p256-sec1.pem

# OpenSSH's: not encrypted, and encrypted (bcrypt_pbkdf, 16 rounds, and
# aes256-ctr: ssh-keygen's default; then its other ciphers).
ssh-keygen -q -t ed25519 -N '' -C plain -f ssh-ed25519-plain
ssh-keygen -q -t ed25519 -N asuka -C enc -f ssh-ed25519
ssh-keygen -q -t ecdsa -b 256 -N asuka -f ssh-p256
ssh-keygen -q -t ecdsa -b 384 -N '' -f ssh-p384-plain
ssh-keygen -q -t ecdsa -b 521 -N asuka -a 4 -f ssh-p521
ssh-keygen -q -t rsa -b 2048 -N asuka -f ssh-rsa
ssh-keygen -q -t rsa -b 3072 -N '' -f ssh-rsa-plain
ssh-keygen -q -t ed25519 -N asuka -Z aes256-gcm@openssh.com -f ssh-ed25519-gcm
ssh-keygen -q -t ed25519 -N asuka -Z chacha20-poly1305@openssh.com -f ssh-ed25519-chacha
ssh-keygen -q -t ecdsa -b 256 -N asuka -Z aes128-cbc -f ssh-p256-cbc
ssh-keygen -q -t ed25519 -N asuka -Z aes128-ctr -f ssh-ed25519-ctr128
for k in ssh-*; do case $k in *.pub) ;; *) mv $k $k.pem ;; esac; done

# The TLS tests' keys (../tls), encrypted: PBES2, and OpenSSH's (ssh-keygen
# rewrites a PEM key it's given in its own format).
$O pkcs8 -topk8 -in ../tls/server.key -passout $P -out tls-server-pbes2.pem
cp ../tls/client.key tls-client-openssh.pem
chmod 600 tls-client-openssh.pem
ssh-keygen -q -p -P '' -N asuka -f tls-client-openssh.pem
