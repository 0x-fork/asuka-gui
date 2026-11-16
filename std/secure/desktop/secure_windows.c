// asuka/secure on Windows: Credential Manager, a generic credential per
// key, named the app's name / the key (as the Windows host's secure store
// named them). Small and quick: answered on the calling thread.
#include "asuka_windows.h"
#include <wincred.h>
#include "secure.asuka.h"

#include <stdlib.h>
#include <string.h>

static wchar_t *target(const char *key) {
	char *t = asuka_sprintf("%s/%s", asuka_desktop_app_name(), key);
	wchar_t *w = asuka_wide(t);
	free(t);
	return w;
}

static void fail(asuka_call *call, const char *what) {
	asuka_error e = {0};
	asuka_win_fail(&e, HRESULT_FROM_WIN32(GetLastError()), what);
	asuka_call_fail(call, &e);
}

static void k_get(secure_Keychain *self, const char *key, asuka_call *call) {
	wchar_t *t = target(key);
	CREDENTIALW *c = NULL;
	BOOL ok = CredReadW(t, CRED_TYPE_GENERIC, 0, &c);
	free(t);
	if (!ok) {
		if (GetLastError() == ERROR_NOT_FOUND) secure_Keychain_get_done(call, NULL);
		else fail(call, "Credential Manager");
		return;
	}
	char *v = asuka_strndup((const char *)c->CredentialBlob, c->CredentialBlobSize);
	CredFree(c);
	secure_Keychain_get_done(call, v);
}

static void k_set(secure_Keychain *self, const char *key, const char *value, asuka_call *call) {
	size_t n = strlen(value);
	if (n > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
		asuka_error e = {0};
		asuka_error_failed(&e, "secure", 0, "the value is longer than the Credential Manager keeps (2560 bytes)");
		asuka_call_fail(call, &e);
		return;
	}
	wchar_t *t = target(key), *user = asuka_wide(key);
	CREDENTIALW c;
	memset(&c, 0, sizeof c);
	c.Type = CRED_TYPE_GENERIC;
	c.TargetName = t;
	c.UserName = user;
	c.CredentialBlobSize = (DWORD)n;
	c.CredentialBlob = (LPBYTE)value;
	c.Persist = CRED_PERSIST_LOCAL_MACHINE;
	BOOL ok = CredWriteW(&c, 0);
	free(t);
	free(user);
	if (ok) secure_Keychain_set_done(call);
	else fail(call, "Credential Manager");
}

static void k_remove(secure_Keychain *self, const char *key, asuka_call *call) {
	wchar_t *t = target(key);
	BOOL ok = CredDeleteW(t, CRED_TYPE_GENERIC, 0);
	free(t);
	if (ok || GetLastError() == ERROR_NOT_FOUND) secure_Keychain_remove_done(call);
	else fail(call, "Credential Manager");
}

static const secure_Keychain_vtable vt = {.get = k_get, .set = k_set, .remove = k_remove};

secure_Keychain *secure_make_Keychain(asuka_error *err) {
	secure_Keychain *k = malloc(sizeof *k);
	k->vt = &vt;
	return k;
}
