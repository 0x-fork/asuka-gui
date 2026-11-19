// tls.c (platform/desktop/src): a client's TLS settings (asuka_desktop.h's
// asuka_tls: std/http's and std/websocket's tls) as the systems' own
// clients take them. Each system's code reads the PEM: Windows' crypt32
// and CNG, Linux's GIO (GnuTLS under it).
//
// Windows: WinHTTP's own check of the server's certificate is turned off
// for a request with authorities of its own (or none at all), and the
// check is this file's, in the status callback WinHTTP calls as the
// request is about to be sent (after the handshake, before a byte of it):
// the chain built by a chain engine whose exclusive roots are the
// settings' (and by the system's first, with trust_system), checked with
// the SSL policy for the server's name, as .NET's WinHttpHandler does. A
// server that isn't trusted has its request closed, unsent. The client's
// certificate is a CERT_CONTEXT whose key CNG holds: Schannel signs in
// LSA's process, which can't use an ephemeral key, so the key is
// persisted under a name of the process's own and deleted as it exits;
// and it sends the chain it builds from the system's stores, so the
// chain's intermediates are added to the user's (intermediates).
//
// Linux: a GTlsFileDatabase of the authorities (read from a temporary
// file, which GnuTLS reads again for each connection: removed as the
// process exits) is the session's database, so GIO checks the
// chain and the name; with trust_system the system's stays, and a chain it
// refuses is checked against the authorities' database in the message's
// accept-certificate. The client's certificate is a GTlsCertificate of
// the certificate and its key, set on each message. Its connections
// neither resume GIO's cached sessions nor add to them (fresh).
#ifdef _WIN32
#include "asuka_windows.h"
#include <wincrypt.h>
#include <ncrypt.h>
#include <winhttp.h>
#else
#include "asuka_linux.h"
#include <pthread.h>
#include <unistd.h>
#endif
#include "asuka_desktop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct asuka_tls {
	struct asuka_tls *next;
	// The settings: what asuka_tls_get finds it by.
	char *ca, *certificate, *key;
	bool trust_system, insecure;
#ifdef _WIN32
	HCERTSTORE roots;         // ca's certificates
	HCERTCHAINENGINE engine;  // a chain engine whose only roots are they
	HCERTSTORE chain;         // the client's certificate and the authorities' after it
	PCCERT_CONTEXT client;    // the client's certificate, its key's container named
	wchar_t *key_name;        // that container: deleted as the process exits
#else
	GTlsDatabase *db;         // ca's certificates
	char *db_file;            // the file it reads them from: removed as the process exits
	GTlsCertificate *client;  // the client's certificate, with its key
#endif
};

static asuka_tls *made;  // under mu

#ifdef _WIN32
static SRWLOCK mu = SRWLOCK_INIT;
#define LOCK() AcquireSRWLockExclusive(&mu)
#define UNLOCK() ReleaseSRWLockExclusive(&mu)
#else
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mu)
#define UNLOCK() pthread_mutex_unlock(&mu)
#endif

static bool same(const char *a, const char *b) { return strcmp(a ? a : "", b ? b : "") == 0; }

#ifdef _WIN32

// ---- Windows ----

// pem_next finds the next PEM block of text from *p: its label (up to 63
// bytes) and its bytes (malloc's), *p past it. false when there's none.
static bool pem_next(const char **p, char label[64], BYTE **der, DWORD *n) {
	for (;;) {
		const char *begin = strstr(*p, "-----BEGIN ");
		if (!begin) return false;
		const char *l = begin + 11;
		const char *le = strstr(l, "-----");
		if (!le || le - l >= 64) return false;
		memcpy(label, l, (size_t)(le - l));
		label[le - l] = 0;
		char end[80];
		snprintf(end, sizeof end, "-----END %s-----", label);
		const char *e = strstr(le, end);
		if (!e) return false;
		e += strlen(end);
		*p = e;
		DWORD len = 0;
		if (!CryptStringToBinaryA(begin, (DWORD)(e - begin), CRYPT_STRING_BASE64HEADER, NULL, &len, NULL, NULL)) continue;
		*der = malloc(len ? len : 1);
		if (!CryptStringToBinaryA(begin, (DWORD)(e - begin), CRYPT_STRING_BASE64HEADER, *der, &len, NULL, NULL)) {
			free(*der);
			continue;
		}
		*n = len;
		return true;
	}
}

// add_certificates adds pem's certificates to store: how many, -1 when
// one can't be read. *first, if not NULL, is the first's context.
static int add_certificates(HCERTSTORE store, const char *pem, PCCERT_CONTEXT *first) {
	int count = 0;
	char label[64];
	BYTE *der;
	DWORD n;
	for (const char *p = pem; pem_next(&p, label, &der, &n);) {
		if (strcmp(label, "CERTIFICATE") != 0) {
			free(der);
			continue;
		}
		PCCERT_CONTEXT c = NULL;
		bool ok = CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, der, n, CERT_STORE_ADD_ALWAYS, first && count == 0 ? &c : NULL);
		free(der);
		if (!ok) return -1;
		if (first && count == 0) *first = c;
		count++;
	}
	return count;
}

// pkcs8 is the private key of pem as PKCS#8's PrivateKeyInfo (LocalAlloc's):
// PRIVATE KEY as it is; RSA PRIVATE KEY (PKCS#1) and EC PRIVATE KEY (SEC 1)
// wrapped in one, the curve from the key's parameters. (An encrypted key,
// ENCRYPTED PRIVATE KEY, would be decrypted here.)
static BYTE *pkcs8(const char *pem, DWORD *len, asuka_error *err) {
	char label[64];
	BYTE *der;
	DWORD n;
	for (const char *p = pem; pem_next(&p, label, &der, &n);) {
		BYTE *out = NULL;
		if (strcmp(label, "PRIVATE KEY") == 0) {
			out = LocalAlloc(LMEM_FIXED, n);
			memcpy(out, der, n);
			*len = n;
		} else if (strcmp(label, "RSA PRIVATE KEY") == 0 || strcmp(label, "EC PRIVATE KEY") == 0) {
			bool rsa = label[0] == 'R';
			CRYPT_PRIVATE_KEY_INFO info = {0};
			info.PrivateKey = (CRYPT_DER_BLOB){n, der};
			BYTE null[] = {0x05, 0x00};
			BYTE *curve = NULL;
			DWORD cn = 0;
			if (rsa) {
				info.Algorithm.pszObjId = szOID_RSA_RSA;
				info.Algorithm.Parameters = (CRYPT_OBJID_BLOB){sizeof null, null};
			} else {
				CRYPT_ECC_PRIVATE_KEY_INFO *ec = NULL;
				DWORD en = 0;
				if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_ECC_PRIVATE_KEY, der, n, CRYPT_DECODE_ALLOC_FLAG, NULL, &ec, &en) || !ec->szCurveOid) {
					if (ec) LocalFree(ec);
					free(der);
					asuka_error_failed(err, "tls", 0, "tls: key: an EC key without its curve");
					return NULL;
				}
				CryptEncodeObjectEx(X509_ASN_ENCODING, X509_OBJECT_IDENTIFIER, &ec->szCurveOid, CRYPT_ENCODE_ALLOC_FLAG, NULL, &curve, &cn);
				LocalFree(ec);
				info.Algorithm.pszObjId = szOID_ECC_PUBLIC_KEY;
				info.Algorithm.Parameters = (CRYPT_OBJID_BLOB){cn, curve};
			}
			bool ok = CryptEncodeObjectEx(X509_ASN_ENCODING, PKCS_PRIVATE_KEY_INFO, &info, CRYPT_ENCODE_ALLOC_FLAG, NULL, &out, len);
			if (curve) LocalFree(curve);
			if (!ok) out = NULL;
		} else {
			free(der);
			continue;
		}
		free(der);
		if (!out) asuka_error_failed(err, "tls", 0, "tls: key can't be read");
		return out;
	}
	asuka_error_failed(err, "tls", 0, "tls: key has no PEM private key (PRIVATE KEY, RSA PRIVATE KEY or EC PRIVATE KEY)");
	return NULL;
}

// delete_keys deletes the keys the process persisted, as it exits.
static void delete_keys(void) {
	NCRYPT_PROV_HANDLE prov = 0;
	if (NCryptOpenStorageProvider(&prov, MS_KEY_STORAGE_PROVIDER, 0) != ERROR_SUCCESS) return;
	for (asuka_tls *t = made; t; t = t->next) {
		NCRYPT_KEY_HANDLE k = 0;
		if (t->key_name && NCryptOpenKey(prov, &k, t->key_name, 0, NCRYPT_SILENT_FLAG) == ERROR_SUCCESS) NCryptDeleteKey(k, NCRYPT_SILENT_FLAG);
	}
	NCryptFreeObject(prov);
}

// client_key has t's client certificate's key in CNG, persisted under a
// name of the process's own, and names it on the certificate.
static bool client_key(asuka_tls *t, const char *key, asuka_error *err) {
	DWORD n = 0;
	BYTE *der = pkcs8(key, &n, err);
	if (!der) return false;
	static LONG count;
	wchar_t name[96];
	_snwprintf(name, sizeof name / sizeof *name, L"Asuka TLS %lu-%ld-%llu", GetCurrentProcessId(), InterlockedIncrement(&count),
	           (unsigned long long)GetTickCount64());
	NCRYPT_PROV_HANDLE prov = 0;
	NCRYPT_KEY_HANDLE k = 0;
	NCryptBuffer b = {(ULONG)((wcslen(name) + 1) * sizeof(wchar_t)), NCRYPTBUFFER_PKCS_KEY_NAME, name};
	NCryptBufferDesc d = {NCRYPTBUFFER_VERSION, 1, &b};
	SECURITY_STATUS st = NCryptOpenStorageProvider(&prov, MS_KEY_STORAGE_PROVIDER, 0);
	if (st == ERROR_SUCCESS) st = NCryptImportKey(prov, 0, NCRYPT_PKCS8_PRIVATE_KEY_BLOB, &d, &k, der, n, NCRYPT_OVERWRITE_KEY_FLAG | NCRYPT_SILENT_FLAG);
	SecureZeroMemory(der, n);
	LocalFree(der);
	if (st != ERROR_SUCCESS) {
		if (prov) NCryptFreeObject(prov);
		asuka_error_failed(err, "tls", (int64_t)st, "tls: key can't be read (0x%08lx)", (unsigned long)st);
		return false;
	}
	static bool hooked;
	if (!hooked) hooked = true, atexit(delete_keys);
	t->key_name = _wcsdup(name);
	NCryptFreeObject(k);
	NCryptFreeObject(prov);
	CRYPT_KEY_PROV_INFO info = {.pwszContainerName = t->key_name, .pwszProvName = (LPWSTR)MS_KEY_STORAGE_PROVIDER};
	HCRYPTPROV_OR_NCRYPT_KEY_HANDLE h = 0;
	DWORD spec = 0;
	BOOL free_h = FALSE;
	if (!CertSetCertificateContextProperty(t->client, CERT_KEY_PROV_INFO_PROP_ID, 0, &info) ||
	    !CryptAcquireCertificatePrivateKey(t->client, CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_COMPARE_KEY_FLAG | CRYPT_ACQUIRE_SILENT_FLAG, NULL, &h,
	                                       &spec, &free_h)) {
		asuka_error_failed(err, "tls", 0, "tls: the key isn't the certificate's");
		return false;
	}
	if (free_h) NCryptFreeObject(h);
	return true;
}

// intermediates adds the authorities' certificates of a client's chain
// (those that aren't roots) to the user's Intermediate Certification
// Authorities: Schannel signs in LSA's process and sends the chain it
// builds from the system's stores, not the certificate's own, so a
// server that trusts only the root would get no intermediate to reach it
// by. That store grants no trust (only roots do), and .NET's
// SslStreamCertificateContext fills it so, for the same reason.
static void intermediates(HCERTSTORE chain, PCCERT_CONTEXT leaf) {
	HCERTSTORE ca = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_OPEN_EXISTING_FLAG, L"CA");
	if (!ca) return;
	for (PCCERT_CONTEXT c = NULL; (c = CertEnumCertificatesInStore(chain, c));) {
		bool root = CertCompareCertificateName(X509_ASN_ENCODING, &c->pCertInfo->Subject, &c->pCertInfo->Issuer);
		if (!root && !CertCompareCertificate(X509_ASN_ENCODING, c->pCertInfo, leaf->pCertInfo)) CertAddCertificateContextToStore(ca, c, CERT_STORE_ADD_USE_EXISTING, NULL);
	}
	CertCloseStore(ca, 0);
}

static bool make(asuka_tls *t, asuka_error *err) {
	if (t->ca && *t->ca) {
		t->roots = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
		int n = t->roots ? add_certificates(t->roots, t->ca, NULL) : -1;
		if (n <= 0) {
			asuka_error_failed(err, "tls", 0, n < 0 ? "tls: ca: a certificate can't be read" : "tls: ca has no PEM certificate");
			return false;
		}
		CERT_CHAIN_ENGINE_CONFIG cfg = {0};
		cfg.cbSize = sizeof cfg;
		cfg.hExclusiveRoot = t->roots;
		cfg.dwExclusiveFlags = CERT_CHAIN_EXCLUSIVE_ENABLE_CA_FLAG;
		if (!CertCreateCertificateChainEngine(&cfg, &t->engine)) {
			asuka_win_fail(err, HRESULT_FROM_WIN32(GetLastError()), "tls: ca");
			return false;
		}
	}
	if (t->certificate && *t->certificate) {
		t->chain = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
		int n = t->chain ? add_certificates(t->chain, t->certificate, &t->client) : -1;
		if (n <= 0) {
			asuka_error_failed(err, "tls", 0, n < 0 ? "tls: certificate can't be read" : "tls: certificate has no PEM certificate");
			return false;
		}
		if (!client_key(t, t->key ? t->key : "", err)) return false;
		intermediates(t->chain, t->client);
	}
	return true;
}

static void unmake(asuka_tls *t) {
	if (t->client) CertFreeCertificateContext(t->client);
	if (t->chain) CertCloseStore(t->chain, 0);
	if (t->engine) CertFreeCertificateChainEngine(t->engine);
	if (t->roots) CertCloseStore(t->roots, 0);
	if (t->key_name) {
		NCRYPT_PROV_HANDLE prov = 0;
		NCRYPT_KEY_HANDLE k = 0;
		if (NCryptOpenStorageProvider(&prov, MS_KEY_STORAGE_PROVIDER, 0) == ERROR_SUCCESS) {
			if (NCryptOpenKey(prov, &k, t->key_name, 0, NCRYPT_SILENT_FLAG) == ERROR_SUCCESS) NCryptDeleteKey(k, NCRYPT_SILENT_FLAG);
			NCryptFreeObject(prov);
		}
		free(t->key_name);
	}
}

// why is what the SSL policy's error says of the certificate.
static const char *why(DWORD e) {
	switch (e) {
	case (DWORD)CERT_E_UNTRUSTEDROOT:
	case (DWORD)CERT_E_CHAINING:
	case (DWORD)TRUST_E_CERT_SIGNATURE:
	case (DWORD)CERT_E_UNTRUSTEDCA:
		return "it isn't signed by an authority trusted";
	case (DWORD)CERT_E_CN_NO_MATCH:
		return "it isn't for that name";
	case (DWORD)CERT_E_EXPIRED:
		return "it expired, or isn't valid yet";
	case (DWORD)CERT_E_WRONG_USAGE:
		return "it isn't a server's";
	case (DWORD)CRYPT_E_REVOKED:
		return "it was revoked";
	default:
		return NULL;
	}
}

// verified is whether engine (NULL: the system's) trusts cert for host,
// *problem the policy's error when it doesn't.
static bool verified(HCERTCHAINENGINE engine, PCCERT_CONTEXT cert, wchar_t *host, DWORD *problem) {
	LPSTR usage = szOID_PKIX_KP_SERVER_AUTH;
	CERT_CHAIN_PARA para = {0};
	para.cbSize = sizeof para;
	para.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
	para.RequestedUsage.Usage.cUsageIdentifier = 1;
	para.RequestedUsage.Usage.rgpszUsageIdentifier = &usage;
	PCCERT_CHAIN_CONTEXT chain = NULL;
	if (!CertGetCertificateChain(engine, cert, NULL, cert->hCertStore, &para, 0, NULL, &chain)) {
		*problem = GetLastError();
		return false;
	}
	SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl = {0};
	ssl.cbSize = sizeof ssl;
	ssl.dwAuthType = AUTHTYPE_SERVER;
	ssl.pwszServerName = host;
	CERT_CHAIN_POLICY_PARA pp = {0};
	pp.cbSize = sizeof pp;
	pp.pvExtraPolicyPara = &ssl;
	CERT_CHAIN_POLICY_STATUS ps = {0};
	ps.cbSize = sizeof ps;
	bool ok = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &pp, &ps) && ps.dwError == 0;
	if (!ok) *problem = ps.dwError ? ps.dwError : (DWORD)CERT_E_UNTRUSTEDROOT;
	CertFreeCertificateChain(chain);
	return ok;
}

// request_url is the URL a request is at (after its redirects), of
// malloc's, its host in *host (malloc's) and whether it's https.
static bool request_host(HINTERNET h, wchar_t **host, bool *https) {
	DWORD n = 0;
	WinHttpQueryOption(h, WINHTTP_OPTION_URL, NULL, &n);
	if (!n) return false;
	wchar_t *url = malloc(n + sizeof(wchar_t));
	bool ok = WinHttpQueryOption(h, WINHTTP_OPTION_URL, url, &n);
	if (ok) {
		url[n / sizeof(wchar_t)] = 0;
		URL_COMPONENTS u = {0};
		u.dwStructSize = sizeof u;
		u.dwHostNameLength = (DWORD)-1;
		u.dwSchemeLength = (DWORD)-1;
		ok = WinHttpCrackUrl(url, 0, 0, &u) && u.lpszHostName;
		if (ok) {
			*https = u.nScheme == INTERNET_SCHEME_HTTPS;
			*host = malloc((u.dwHostNameLength + 1) * sizeof(wchar_t));
			memcpy(*host, u.lpszHostName, u.dwHostNameLength * sizeof(wchar_t));
			(*host)[u.dwHostNameLength] = 0;
		}
	}
	free(url);
	return ok;
}

// refused says why WinHTTP's own check refused the server's certificate
// (WINHTTP_CALLBACK_STATUS_SECURE_FAILURE's flags), NULL when that isn't
// why the connection failed.
static const char *refused(DWORD flags) {
	if (flags & WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA) return "it isn't signed by an authority trusted";
	if (flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID) return "it isn't for that name";
	if (flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID) return "it expired, or isn't valid yet";
	if (flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED) return "it was revoked";
	if (flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_WRONG_USAGE) return "it isn't a server's";
	if (flags & (WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT | WINHTTP_CALLBACK_STATUS_FLAG_CERT_REV_FAILED)) return "it can't be checked";
	return NULL;
}

// on_status checks the server's certificate as the request is about to be
// sent to it (one that isn't trusted has the request closed), and hears
// why WinHTTP's own check refused one.
static void CALLBACK on_status(HINTERNET h, DWORD_PTR ctx, DWORD status, LPVOID info, DWORD len) {
	asuka_win_tls *c = (asuka_win_tls *)ctx;
	if (!c || c->untrusted) return;
	if (status == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE) {
		const char *w = info && len >= sizeof(DWORD) ? refused(*(DWORD *)info) : NULL;
		wchar_t *host = NULL;
		bool https = false;
		if (w && request_host(h, &host, &https)) {
			char *name = asuka_narrow(host, -1);
			c->untrusted = asuka_sprintf("%s's certificate isn't trusted: %s", name, w);
			free(name);
		}
		free(host);
		return;
	}
	if (status != WINHTTP_CALLBACK_STATUS_SENDING_REQUEST || !c->tls) return;
	wchar_t *host = NULL;
	bool https = false;
	if (!request_host(h, &host, &https) || !https) {
		free(host);
		return;
	}
	PCCERT_CONTEXT cert = NULL;
	DWORD n = sizeof cert;
	DWORD problem = 0;
	bool ok = WinHttpQueryOption(h, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &cert, &n) && cert;
	if (ok) {
		const asuka_tls *t = c->tls;
		ok = (t->trust_system && verified(NULL, cert, host, &problem)) || (t->engine && verified(t->engine, cert, host, &problem));
	}
	if (!ok) {
		char *name = asuka_narrow(host, -1);
		const char *w = why(problem);
		c->untrusted = w ? asuka_sprintf("%s's certificate isn't trusted: %s", name, w)
		                 : asuka_sprintf("%s's certificate isn't trusted (0x%08lx)", name, (unsigned long)problem);
		free(name);
		c->close(c->ctx);
	}
	if (cert) CertFreeCertificateContext(cert);
	free(host);
}

bool asuka_win_tls_begin(void *request, asuka_win_tls *c, asuka_error *err) {
	const asuka_tls *t = c->tls;
	HINTERNET req = request;
	// The client's certificate, or none: a server that asks for one it
	// doesn't require gets none (WinHTTP fails the request otherwise),
	// as on the other systems. (An http: request takes neither.)
	WinHttpSetOption(req, WINHTTP_OPTION_CLIENT_CERT_CONTEXT, t && t->client ? (void *)t->client : WINHTTP_NO_CLIENT_CERT_CONTEXT,
	                 t && t->client ? sizeof(CERT_CONTEXT) : 0);
	bool own = t && (t->insecure || t->engine);  // the check is this file's, or none
	if (own) {
		DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
		              SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
		if (!WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof flags)) {
			asuka_win_fail(err, HRESULT_FROM_WIN32(GetLastError()), "tls");
			return false;
		}
		if (t->insecure) return true;
	}
	// The system's check says why it refused a server; this file's checks
	// it as the request is about to be sent.
	DWORD_PTR ctx = (DWORD_PTR)c;
	DWORD events = WINHTTP_CALLBACK_FLAG_SECURE_FAILURE | (own ? WINHTTP_CALLBACK_FLAG_SEND_REQUEST : 0);
	if (!WinHttpSetOption(req, WINHTTP_OPTION_CONTEXT_VALUE, &ctx, sizeof ctx) ||
	    WinHttpSetStatusCallback(req, on_status, events, 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
		asuka_win_fail(err, HRESULT_FROM_WIN32(GetLastError()), "tls");
		return false;
	}
	return true;
}

bool asuka_win_tls_failed(asuka_win_tls *c, asuka_error *err) {
	if (!c->untrusted) return false;
	asuka_error_failed(err, "tls", 0, "%s", c->untrusted);
	free(c->untrusted);
	c->untrusted = NULL;
	return true;
}

#else

// ---- Linux ----

// remove_files removes the authorities' files, as the process exits.
static void remove_files(void) {
	for (asuka_tls *t = made; t; t = t->next)
		if (t->db_file) remove(t->db_file);
}

static bool make(asuka_tls *t, asuka_error *err) {
	if (!asuka_linux() || !asuka_linux_load(ASUKA_LIB_SOUP) || !G(g_tls_file_database_new) || !G(g_tls_certificate_new_from_pem)) {
		asuka_error_unsupported(err, "TLS settings need libsoup 3 and GIO's TLS");
		return false;
	}
	GError *e = NULL;
	if (t->ca && *t->ca) {
		// GIO's database of anchors reads a file, again as each connection
		// starts: one of the process's own, removed as it exits.
		char *name = NULL;
		int fd = G(g_file_open_tmp)("asuka-ca-XXXXXX.pem", &name, &e);
		if (fd < 0) {
			asuka_linux_fail(err, e, "tls: ca");
			return false;
		}
		close(fd);
		t->db_file = asuka_strdup(name);
		G(g_free)(name);
		static bool hooked;
		if (!hooked) hooked = true, atexit(remove_files);
		if (G(g_file_set_contents)(t->db_file, t->ca, -1, &e)) t->db = G(g_tls_file_database_new)(t->db_file, &e);
		if (!t->db) {
			asuka_linux_fail(err, e, "tls: ca");
			return false;
		}
	}
	if (t->certificate && *t->certificate) {
		// (An encrypted key, ENCRYPTED PRIVATE KEY, would be decrypted
		// here: GIO reads PKCS#8, PKCS#1 and SEC 1 keys, not encrypted.)
		char *pem = asuka_sprintf("%s\n%s\n", t->certificate, t->key ? t->key : "");
		t->client = G(g_tls_certificate_new_from_pem)(pem, -1, &e);
		free(pem);
		if (!t->client) {
			asuka_linux_fail(err, e, "tls: certificate and key");
			return false;
		}
	}
	return true;
}

static void unmake(asuka_tls *t) {
	if (t->db) G(g_object_unref)(t->db);
	if (t->client) G(g_object_unref)(t->client);
	if (t->db_file) remove(t->db_file), free(t->db_file);
}

static const char *problems(GTlsCertificateFlags f) {
	if (f & G_TLS_CERTIFICATE_UNKNOWN_CA) return "it isn't signed by an authority trusted";
	if (f & G_TLS_CERTIFICATE_BAD_IDENTITY) return "it isn't for that name";
	if (f & G_TLS_CERTIFICATE_EXPIRED) return "it expired";
	if (f & G_TLS_CERTIFICATE_NOT_ACTIVATED) return "it isn't valid yet";
	if (f & G_TLS_CERTIFICATE_REVOKED) return "it was revoked";
	if (f & G_TLS_CERTIFICATE_INSECURE) return "its algorithm is insecure";
	return "it can't be checked";
}

// accept is a message's accept-certificate: the chain the session's
// database refused (errors, its reasons), accepted after all when nothing's
// checked, or (trust_system) when the settings' authorities trust it.
static gboolean accept(SoupMessage *msg, GTlsCertificate *cert, GTlsCertificateFlags errors, gpointer p) {
	const asuka_tls *t = p;
	if (t->insecure) return true;
	GUri *u = G(soup_message_get_uri)(msg);
	const char *host = u ? G(g_uri_get_host)(u) : NULL;
	if (t->trust_system && t->db && host) {
		int port = G(g_uri_get_port)(u);
		GSocketConnectable *id = G(g_network_address_new)(host, (guint16)(port > 0 ? port : 443));
		GTlsCertificateFlags f = G(g_tls_database_verify_chain)(t->db, cert, G_TLS_DATABASE_PURPOSE_AUTHENTICATE_SERVER, id, NULL,
		                                                       G_TLS_DATABASE_VERIFY_NONE, NULL, NULL);
		G(g_object_unref)(id);
		if (f == 0) return true;
		errors = f;
	}
	char *why = asuka_sprintf("%s's certificate isn't trusted: %s", host ? host : "the server", problems(errors));
	G(g_object_set_data_full)((GObject *)msg, "asuka-tls-untrusted", why, free);
	return false;
}

// G_SOCKET_CLIENT_TLS_HANDSHAKING: the connection a message's
// network-event gives is the TLS client's, its handshake about to start.
enum { tls_handshaking = 6 };

// fresh has a connection do a full handshake, not resume a session:
// GIO's cache of sessions is the process's, by server, so a session the
// system's trust (or other settings) let in would be resumed without the
// settings' check, the client's certificate unshown; and this one is
// kept out of it, for the same reason the other way.
static void fresh(SoupMessage *msg, int event, GIOStream *connection, gpointer p) {
	(void)msg, (void)p;
	if (event == tls_handshaking && connection) G(g_object_set)(connection, "session-resumption-enabled", (gboolean)0, NULL);
}

void asuka_soup_tls(SoupSession *s, SoupMessage *msg, const asuka_tls *t) {
	if (!t) return;
	G(g_signal_connect_data)(msg, "network-event", (GCallback)fresh, NULL, NULL, 0);
	if (t->db && !t->trust_system) G(soup_session_set_tls_database)(s, t->db);
	if (t->client) G(soup_message_set_tls_client_certificate)(msg, t->client);
	if (t->db || t->insecure) G(g_signal_connect_data)(msg, "accept-certificate", (GCallback)accept, (gpointer)t, NULL, 0);
}

const char *asuka_soup_untrusted(SoupMessage *msg) { return G(g_object_get_data)((GObject *)msg, "asuka-tls-untrusted"); }

#endif

asuka_tls *asuka_tls_get(const char *ca, bool trust_system, const char *certificate, const char *key, bool insecure, asuka_error *err) {
	LOCK();
	asuka_tls *t = made;
	while (t && !(same(t->ca, ca) && same(t->certificate, certificate) && same(t->key, key) && t->trust_system == trust_system && t->insecure == insecure))
		t = t->next;
	if (!t) {
		t = calloc(1, sizeof *t);
		t->ca = asuka_strdup(ca ? ca : "");
		t->certificate = asuka_strdup(certificate ? certificate : "");
		t->key = asuka_strdup(key ? key : "");
		t->trust_system = trust_system;
		t->insecure = insecure;
		if (make(t, err)) {
			t->next = made;
			made = t;
		} else {
			unmake(t);
			free(t->ca);
			free(t->certificate);
			free(t->key);
			free(t);
			t = NULL;
		}
	}
	UNLOCK();
	return t;
}
