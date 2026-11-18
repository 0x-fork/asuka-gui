// tlstrust.c: the system's trust store, as std/net's TLS (tls.c) checks a
// server's certificate against it by default.
//
// Apple's systems and Windows check a chain themselves, with what the
// user and the administrator trust or distrust, intermediates fetched,
// revocation: SecTrustEvaluateWithError (Security.framework) and
// CertGetCertificateChain with CertVerifyCertificateChainPolicy's SSL
// policy (crypt32), each loaded as TLS first checks one (dlopen,
// LoadLibrary), so no build links them. They may block: a thread of its
// own calls them.
//
// Elsewhere the system's authorities are read once, and Mbed TLS checks
// the chain against them: on Linux (glibc's and musl's builds alike) as
// Go reads them, SSL_CERT_FILE or the first bundle of the usual paths
// that has any, else SSL_CERT_DIR's or the usual directories' files; on
// Android, the system's store (Conscrypt's updatable one since Android
// 14, /system/etc/security/cacerts before).
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#endif

#if !defined(__wasm__)

#include "std/sock.h"  // before windows.h (base/thread.h's)
#include "std/tls.h"
#include "base/thread.h"
#include "std/mbedtls/mbedtls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <dlfcn.h>
#elif !defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#endif

// The problems tls.c names (its problems[]).
enum { P_EXPIRED = 1, P_FUTURE, P_HOST, P_ISSUER, P_REVOKED, P_MISSING, P_OTHER };

#if defined(__APPLE__)

// ---- Apple: SecTrust ----

// CoreFoundation's and Security's types and calls, declared here (their
// headers are Objective-C-minded; these are plain C).
typedef const void *CFTypeRef;
typedef const struct __CFData *CFDataRef;
typedef const struct __CFArray *CFArrayRef;
typedef const struct __CFString *CFStringRef;
typedef struct __CFError *CFErrorRef;
typedef const struct __CFAllocator *CFAllocatorRef;
typedef long CFIndex;
typedef struct __SecCertificate *SecCertificateRef;
typedef struct __SecPolicy *SecPolicyRef;
typedef struct __SecTrust *SecTrustRef;
typedef int32_t OSStatus;
typedef struct {
	CFIndex version;
	const void *retain, *release, *copyDescription, *equal;
} CFArrayCallBacks;

static struct {
	bool tried, ok;
	CFDataRef (*CFDataCreate)(CFAllocatorRef, const uint8_t *, CFIndex);
	CFArrayRef (*CFArrayCreate)(CFAllocatorRef, const void **, CFIndex, const CFArrayCallBacks *);
	CFStringRef (*CFStringCreateWithCString)(CFAllocatorRef, const char *, uint32_t);
	bool (*CFStringGetCString)(CFStringRef, char *, CFIndex, uint32_t);
	void (*CFRelease)(CFTypeRef);
	CFIndex (*CFErrorGetCode)(CFErrorRef);
	CFStringRef (*CFErrorCopyDescription)(CFErrorRef);
	const CFArrayCallBacks *kCFTypeArrayCallBacks;
	SecCertificateRef (*SecCertificateCreateWithData)(CFAllocatorRef, CFDataRef);
	SecPolicyRef (*SecPolicyCreateSSL)(bool, CFStringRef);
	OSStatus (*SecTrustCreateWithCertificates)(CFTypeRef, CFTypeRef, SecTrustRef *);
	bool (*SecTrustEvaluateWithError)(SecTrustRef, CFErrorRef *);
} A;

static ak_mutex apple_mu = AK_MUTEX_INIT;

static bool apple_load(void) {
	ak_mutex_lock(&apple_mu);
	if (!A.tried) {
		A.tried = true;
		// By their paths (a program links neither); else as the process
		// has them already (an app's Foundation brings both).
		void *cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LAZY);
		void *sec = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY);
		if (!cf) cf = RTLD_DEFAULT;
		if (!sec) sec = RTLD_DEFAULT;
		{
#define CF(name) *(void **)&A.name = dlsym(cf, #name)
#define SEC(name) *(void **)&A.name = dlsym(sec, #name)
			CF(CFDataCreate);
			CF(CFArrayCreate);
			CF(CFStringCreateWithCString);
			CF(CFStringGetCString);
			CF(CFRelease);
			CF(CFErrorGetCode);
			CF(CFErrorCopyDescription);
			CF(kCFTypeArrayCallBacks);
			SEC(SecCertificateCreateWithData);
			SEC(SecPolicyCreateSSL);
			SEC(SecTrustCreateWithCertificates);
			SEC(SecTrustEvaluateWithError);
#undef CF
#undef SEC
			A.ok = A.CFDataCreate && A.CFArrayCreate && A.CFStringCreateWithCString && A.CFStringGetCString && A.CFRelease && A.CFErrorGetCode && A.CFErrorCopyDescription && A.kCFTypeArrayCallBacks && A.SecCertificateCreateWithData && A.SecPolicyCreateSSL && A.SecTrustCreateWithCertificates && A.SecTrustEvaluateWithError;
		}
	}
	bool ok = A.ok;
	ak_mutex_unlock(&apple_mu);
	return ok;
}

bool ak_tls_system_verifier(void) { return apple_load(); }

#define UTF8 0x08000100u  // kCFStringEncodingUTF8

bool ak_tls_system_verify(const char *host, const uint8_t *const *der, const size_t *len, uint32_t n, int *problem, char *why, size_t cap) {
	*problem = P_ISSUER;
	why[0] = 0;
	if (!apple_load() || n == 0) {
		snprintf(why, cap, "the system's trust store can't be reached");
		return false;
	}
	const void **certs = calloc(n, sizeof(void *));
	uint32_t made = 0;
	for (uint32_t i = 0; i < n; i++) {
		CFDataRef d = A.CFDataCreate(NULL, der[i], (CFIndex)len[i]);
		SecCertificateRef c = d ? A.SecCertificateCreateWithData(NULL, d) : NULL;
		if (d) A.CFRelease(d);
		if (!c) {
			if (i == 0) break;
			continue;  // an extra the server sent that can't be read: left out
		}
		certs[made++] = c;
	}
	bool ok = false;
	if (made && certs[0]) {
		CFArrayRef arr = A.CFArrayCreate(NULL, certs, (CFIndex)made, A.kCFTypeArrayCallBacks);
		CFStringRef name = A.CFStringCreateWithCString(NULL, host, UTF8);
		SecPolicyRef pol = A.SecPolicyCreateSSL(true, name);
		SecTrustRef trust = NULL;
		if (arr && pol && A.SecTrustCreateWithCertificates(arr, pol, &trust) == 0 && trust) {
			CFErrorRef err = NULL;
			ok = A.SecTrustEvaluateWithError(trust, &err);
			if (!ok && err) {
				CFIndex code = A.CFErrorGetCode(err);
				switch (code) {
				case -67818: *problem = P_EXPIRED; break;   // errSecCertificateExpired
				case -67819: *problem = P_FUTURE; break;    // errSecCertificateNotValidYet
				case -67820: *problem = P_REVOKED; break;   // errSecCertificateRevoked
				case -67602: *problem = P_HOST; break;      // errSecHostNameMismatch
				case -67843: *problem = P_ISSUER; break;    // errSecNotTrusted
				}
				CFStringRef desc = A.CFErrorCopyDescription(err);
				if (desc) {
					if (!A.CFStringGetCString(desc, why, (CFIndex)cap, UTF8)) why[0] = 0;
					A.CFRelease(desc);
				}
				A.CFRelease(err);
			}
			A.CFRelease(trust);
		} else {
			snprintf(why, cap, "the system couldn't read the certificate");
		}
		if (pol) A.CFRelease(pol);
		if (name) A.CFRelease(name);
		if (arr) A.CFRelease(arr);
	} else {
		*problem = P_OTHER;
		snprintf(why, cap, "the system couldn't read the certificate");
	}
	for (uint32_t i = 0; i < made; i++) A.CFRelease(certs[i]);
	free(certs);
	return ok;
}

struct mbedtls_x509_crt *ak_tls_system_roots(char *why, size_t cap) {
	snprintf(why, cap, "the system checks certificates itself");
	return NULL;
}

#elif defined(_WIN32)

// ---- Windows: CertGetCertificateChain ----

static struct {
	bool tried, ok;
	HCERTSTORE(WINAPI *CertOpenStore)(LPCSTR, DWORD, HCRYPTPROV_LEGACY, DWORD, const void *);
	BOOL(WINAPI *CertAddEncodedCertificateToStore)(HCERTSTORE, DWORD, const BYTE *, DWORD, DWORD, PCCERT_CONTEXT *);
	BOOL(WINAPI *CertGetCertificateChain)(HCERTCHAINENGINE, PCCERT_CONTEXT, LPFILETIME, HCERTSTORE, PCERT_CHAIN_PARA, DWORD, LPVOID, PCCERT_CHAIN_CONTEXT *);
	BOOL(WINAPI *CertVerifyCertificateChainPolicy)(LPCSTR, PCCERT_CHAIN_CONTEXT, PCERT_CHAIN_POLICY_PARA, PCERT_CHAIN_POLICY_STATUS);
	VOID(WINAPI *CertFreeCertificateChain)(PCCERT_CHAIN_CONTEXT);
	BOOL(WINAPI *CertFreeCertificateContext)(PCCERT_CONTEXT);
	BOOL(WINAPI *CertCloseStore)(HCERTSTORE, DWORD);
} W;

static ak_mutex win_mu = AK_MUTEX_INIT;

static bool win_load(void) {
	ak_mutex_lock(&win_mu);
	if (!W.tried) {
		W.tried = true;
		HMODULE m = LoadLibraryW(L"crypt32.dll");
		if (m) {
#define F(name) *(FARPROC *)&W.name = GetProcAddress(m, #name)
			F(CertOpenStore);
			F(CertAddEncodedCertificateToStore);
			F(CertGetCertificateChain);
			F(CertVerifyCertificateChainPolicy);
			F(CertFreeCertificateChain);
			F(CertFreeCertificateContext);
			F(CertCloseStore);
#undef F
			W.ok = W.CertOpenStore && W.CertAddEncodedCertificateToStore && W.CertGetCertificateChain && W.CertVerifyCertificateChainPolicy && W.CertFreeCertificateChain && W.CertFreeCertificateContext && W.CertCloseStore;
		}
	}
	bool ok = W.ok;
	ak_mutex_unlock(&win_mu);
	return ok;
}

bool ak_tls_system_verifier(void) { return win_load(); }

static void win_text(DWORD e, char *why, size_t cap) {
	char b[256];
	DWORD k = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, b, sizeof b, NULL);
	while (k && (b[k - 1] == '\n' || b[k - 1] == '\r' || b[k - 1] == ' ' || b[k - 1] == '.')) b[--k] = 0;
	if (k) snprintf(why, cap, "%s", b);
	else snprintf(why, cap, "error 0x%08lX", (unsigned long)e);
}

bool ak_tls_system_verify(const char *host, const uint8_t *const *der, const size_t *len, uint32_t n, int *problem, char *why, size_t cap) {
	*problem = P_ISSUER;
	why[0] = 0;
	if (!win_load() || n == 0) {
		snprintf(why, cap, "the system's trust store can't be reached");
		return false;
	}
	// The leaf, and what the server sent beside it, in a store of their
	// own: the chain is built from them and the system's.
	HCERTSTORE store = W.CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_DEFER_CLOSE_UNTIL_LAST_FREE_FLAG, NULL);
	if (!store) {
		snprintf(why, cap, "the system's trust store can't be reached");
		return false;
	}
	PCCERT_CONTEXT leaf = NULL;
	for (uint32_t i = 0; i < n; i++) {
		PCCERT_CONTEXT c = NULL;
		if (!W.CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der[i], (DWORD)len[i], CERT_STORE_ADD_ALWAYS, i == 0 ? &c : NULL)) {
			if (i == 0) break;
			continue;
		}
		if (i == 0) leaf = c;
	}
	bool ok = false;
	if (leaf) {
		// Server authentication (and the old SGC usages some roots still
		// name, as Go asks for them).
		LPSTR usages[] = {szOID_PKIX_KP_SERVER_AUTH, szOID_SERVER_GATED_CRYPTO, szOID_SGC_NETSCAPE};
		CERT_CHAIN_PARA para;
		memset(&para, 0, sizeof para);
		para.cbSize = sizeof para;
		para.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
		para.RequestedUsage.Usage.cUsageIdentifier = 3;
		para.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
		PCCERT_CHAIN_CONTEXT chain = NULL;
		if (W.CertGetCertificateChain(NULL, leaf, NULL, store, &para, CERT_CHAIN_RETURN_LOWER_QUALITY_CONTEXTS, NULL, &chain) && chain) {
			int wn = MultiByteToWideChar(CP_UTF8, 0, host, -1, NULL, 0);
			WCHAR *wide = calloc((size_t)(wn > 0 ? wn : 1), sizeof(WCHAR));
			if (wn > 0) MultiByteToWideChar(CP_UTF8, 0, host, -1, wide, wn);
			SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl;
			memset(&ssl, 0, sizeof ssl);
			ssl.cbSize = sizeof ssl;
			ssl.dwAuthType = AUTHTYPE_SERVER;
			ssl.pwszServerName = wide;
			CERT_CHAIN_POLICY_PARA pp;
			memset(&pp, 0, sizeof pp);
			pp.cbSize = sizeof pp;
			pp.pvExtraPolicyPara = &ssl;
			CERT_CHAIN_POLICY_STATUS st;
			memset(&st, 0, sizeof st);
			st.cbSize = sizeof st;
			if (W.CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &pp, &st)) {
				ok = st.dwError == 0;
				if (!ok) {
					switch ((DWORD)st.dwError) {
					case (DWORD)CERT_E_EXPIRED: *problem = P_EXPIRED; break;
					case (DWORD)CERT_E_CN_NO_MATCH: *problem = P_HOST; break;
					case (DWORD)CRYPT_E_REVOKED: *problem = P_REVOKED; break;
					case (DWORD)CERT_E_UNTRUSTEDROOT:
					case (DWORD)CERT_E_CHAINING: *problem = P_ISSUER; break;
					default: *problem = P_OTHER;
					}
					win_text((DWORD)st.dwError, why, cap);
				}
			} else {
				win_text(GetLastError(), why, cap);
			}
			free(wide);
			W.CertFreeCertificateChain(chain);
		} else {
			win_text(GetLastError(), why, cap);
		}
		W.CertFreeCertificateContext(leaf);
	} else {
		*problem = P_OTHER;
		snprintf(why, cap, "the system couldn't read the certificate");
	}
	W.CertCloseStore(store, 0);
	return ok;
}

struct mbedtls_x509_crt *ak_tls_system_roots(char *why, size_t cap) {
	snprintf(why, cap, "the system checks certificates itself");
	return NULL;
}

#else

// ---- Linux, Android: the authorities' files ----

bool ak_tls_system_verifier(void) { return false; }

bool ak_tls_system_verify(const char *host, const uint8_t *const *der, const size_t *len, uint32_t n, int *problem, char *why, size_t cap) {
	AK_UNUSED(host), AK_UNUSED(der), AK_UNUSED(len), AK_UNUSED(n);
	*problem = P_ISSUER;
	snprintf(why, cap, "the system doesn't check certificates itself");
	return false;
}

static ak_mutex roots_mu = AK_MUTEX_INIT;
static bool roots_read;
static mbedtls_x509_crt roots;
static int nroots;
static char roots_why[300];

// add_file adds the certificates in the file at path (PEM, maybe among
// other text: Android's files have it after): how many.
static int add_file(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	struct stat st;
	if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > (64 << 20)) {
		fclose(f);
		return 0;
	}
	size_t n = (size_t)st.st_size;
	char *b = malloc(n + 1);
	size_t got = b ? fread(b, 1, n, f) : 0;
	fclose(f);
	if (!b || got != n) {
		free(b);
		return 0;
	}
	b[n] = 0;
	int before = 0;
	for (mbedtls_x509_crt *c = &roots; c && c->raw.p; c = c->next) before++;
	// A certificate that can't be read is left out; the rest are kept.
	if (strstr(b, "-----BEGIN CERTIFICATE-----")) mbedtls_x509_crt_parse(&roots, (const unsigned char *)b, n + 1);
	free(b);
	int after = 0;
	for (mbedtls_x509_crt *c = &roots; c && c->raw.p; c = c->next) after++;
	return after - before;
}

static int add_dir(const char *dir) {
	DIR *d = opendir(dir);
	if (!d) return 0;
	int k = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[1024 + 300];
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		k += add_file(p);
	}
	closedir(d);
	return k;
}

#if !defined(__ANDROID__)
// add_dirs adds each directory of list, separated by colons.
static int add_dirs(const char *list) {
	int k = 0;
	while (*list) {
		const char *end = strchr(list, ':');
		size_t n = end ? (size_t)(end - list) : strlen(list);
		char dir[1024];
		if (n && n < sizeof dir) {
			memcpy(dir, list, n);
			dir[n] = 0;
			k += add_dir(dir);
		}
		list += n + (end ? 1 : 0);
	}
	return k;
}
#endif

static void read_roots(void) {
	mbedtls_x509_crt_init(&roots);
#if defined(__ANDROID__)
	// Conscrypt's store (updated with the system's modules, Android 14 and
	// later), else the system image's.
	static const char *const dirs[] = {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"};
	for (size_t i = 0; i < sizeof dirs / sizeof dirs[0] && !nroots; i++) nroots = add_dir(dirs[i]);
	if (!nroots) snprintf(roots_why, sizeof roots_why, "no trusted certificate authorities were found in the system's store (/system/etc/security/cacerts)");
#else
	// As Go reads them (crypto/x509's root_linux.go, root_unix.go).
	static const char *const files[] = {
		"/etc/ssl/certs/ca-certificates.crt",                 // Debian, Ubuntu, Gentoo, Arch
		"/etc/pki/tls/certs/ca-bundle.crt",                   // Fedora, RHEL 6
		"/etc/ssl/ca-bundle.pem",                             // openSUSE
		"/etc/pki/tls/cacert.pem",                            // OpenELEC
		"/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",  // CentOS, RHEL 7
		"/etc/ssl/cert.pem",                                  // Alpine
	};
	const char *file = getenv("SSL_CERT_FILE");
	const char *dirs = getenv("SSL_CERT_DIR");
	if (file && file[0]) {
		nroots = add_file(file);
	} else {
		for (size_t i = 0; i < sizeof files / sizeof files[0] && !nroots; i++) nroots = add_file(files[i]);
	}
	if (dirs && dirs[0]) nroots += add_dirs(dirs);
	else if (!nroots) nroots = add_dirs("/etc/ssl/certs:/etc/pki/tls/certs");
	if (!nroots) snprintf(roots_why, sizeof roots_why, "no trusted certificate authorities were found on this system (a bundle at /etc/ssl/certs/ca-certificates.crt or the like, or SSL_CERT_FILE; the ca-certificates package has them)");
#endif
}

struct mbedtls_x509_crt *ak_tls_system_roots(char *why, size_t cap) {
	ak_mutex_lock(&roots_mu);
	if (!roots_read) {
		roots_read = true;
		read_roots();
	}
	ak_mutex_unlock(&roots_mu);
	if (!nroots) {
		snprintf(why, cap, "%s", roots_why);
		return NULL;
	}
	return &roots;
}

#endif

#endif
