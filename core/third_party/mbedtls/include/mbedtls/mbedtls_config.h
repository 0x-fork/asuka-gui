// mbedtls/mbedtls_config.h: Asuka's Mbed TLS configuration (in place of
// the release's): TLS 1.2 and 1.3, client and server, X.509
// certificates. Written by internal/mbedtlsgen; edit it there.
//
// TLS 1.2 with ECDHE (RSA or ECDSA certificates) and AEAD ciphers only:
// no RSA key exchange, no CBC, no renegotiation, no compression. TLS
// 1.3 with ephemeral (EC)DHE. SNI and ALPN. Sessions resumed with
// tickets: TLS 1.3's PSK, always with (EC)DHE (forward secrecy), and TLS
// 1.2's (RFC 5077); a server's tickets sealed by the ticket module (keys
// in memory, rotated). No early data (0-RTT: replayable), no session ID
// cache on a server, no DTLS.
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

#define MBEDTLS_CONFIG_VERSION 0x04000000

#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_SSL_ALL_ALERT_MESSAGES
#define MBEDTLS_SSL_ALPN
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_PROTO_TLS1_3
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_SESSION_TICKETS
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_TICKET_C
#define MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL_ENABLED
#define MBEDTLS_SSL_TLS_C

#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_RSASSA_PSS_SUPPORT
#define MBEDTLS_X509_USE_C

#endif
