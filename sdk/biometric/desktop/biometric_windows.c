// platform/biometric on Windows: Windows Hello (WinRT's
// UserConsentVerifier). The kind is the sensor Windows Hello has (the
// Windows Biometric Framework's); enrolled is a sensor and Windows Hello
// set up, the passcode Windows Hello set up (its PIN). Its prompt shows
// over the window the user works in, with the reason; it always offers
// the PIN, and names its own cancel button: with no passcode asked, it
// shows only where there's a sensor. macOS runs the package's Swift;
// Linux has none (Asuka's side answers none and notAvailable).
#include "asuka_windows.h"
#include "biometric.asuka.h"

#include <stdlib.h>

static const GUID iid_statics = {0xAF4F3F91, 0x564C, 0x4DDC, {0xB8, 0xB8, 0x97, 0x34, 0x47, 0x62, 0x7C, 0x65}};
static const GUID iid_interop = {0x39E050C3, 0x4E74, 0x441A, {0x8D, 0xC0, 0xB8, 0x11, 0x04, 0xDF, 0x94, 0x9C}};
#define AVAILABILITY "enum(Windows.Security.Credentials.UI.UserConsentVerifierAvailability;i4)"
#define VERIFICATION "enum(Windows.Security.Credentials.UI.UserConsentVerificationResult;i4)"

enum { available = 0 };                                               // UserConsentVerifierAvailability
enum { verified = 0, no_device, not_configured, disabled, busy, retries, canceled };  // UserConsentVerificationResult

// sensor is the PC's biometric sensor for Windows Hello: 2 face, 1
// fingerprint, 0 none.
static int sensor(void) {
	static HRESULT(WINAPI * enum_units)(ULONG, void **, SIZE_T *);
	static HRESULT(WINAPI * bio_free)(void *);
	static bool loaded;
	if (!loaded) {
		loaded = true;
		HMODULE m = LoadLibraryExW(L"winbio.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (m) {
			*(FARPROC *)&enum_units = GetProcAddress(m, "WinBioEnumBiometricUnits");
			*(FARPROC *)&bio_free = GetProcAddress(m, "WinBioFree");
		}
	}
	if (!enum_units) return 0;
	static const ULONG types[] = {0x2, 0x8};  // WINBIO_TYPE_FACIAL_FEATURES, _FINGERPRINT
	static const int kinds[] = {2, 1};
	for (int i = 0; i < 2; i++) {
		void *units = NULL;
		SIZE_T n = 0;
		if (SUCCEEDED(enum_units(types[i], &units, &n))) {
			if (units && bio_free) bio_free(units);
			if (n > 0) return kinds[i];
		}
	}
	return 0;
}

// availability asks Windows Hello whether it's set up, waiting (a worker).
static int availability(asuka_call *call) {
	void *st = NULL, *op = NULL;
	int a = 1;  // DeviceNotPresent
	if (FAILED(asuka_winrt_factory("Windows.Security.Credentials.UI.UserConsentVerifier", &iid_statics, &st))) return a;
	if (SUCCEEDED(ASUKA_SLOT(st, 6, HRESULT(STDMETHODCALLTYPE *)(void *, void **))(st, &op)) && op) {  // CheckAvailabilityAsync
		GUID h = asuka_winrt_iid("pinterface(" ASUKA_WINRT_ASYNC_HANDLER ";" AVAILABILITY ")");
		if (asuka_winrt_wait(op, &h, call) == S_OK) ASUKA_SLOT(op, 8, HRESULT(STDMETHODCALLTYPE *)(void *, int *))(op, &a);  // GetResults
		asuka_com_release(op);
	}
	asuka_com_release(st);
	return a;
}

static void check_run(void *p) {
	asuka_call *call = p;
	asuka_winrt();
	int kind = sensor();
	bool set = availability(call) == available;
	biometric_Status s = {.kind = kind == 2 ? biometric_Kind_face : kind == 1 ? biometric_Kind_fingerprint : biometric_Kind_none,
	                      .enrolled = set && kind != 0, .passcode = set};
	biometric_Authenticator_check_done(call, s);
}

static void spawn(void (*fn)(void *), void *p, asuka_call *call) {
	if (!asuka_desktop_spawn(fn, p)) {
		asuka_error e = {0};
		asuka_error_failed(&e, "biometric", 0, "no thread");
		asuka_call_fail(call, &e);
	}
}

static void b_check(biometric_Authenticator *self, asuka_call *call) { spawn(check_run, call, call); }

typedef struct {
	char *reason;
	bool no_passcode;
	asuka_call *call;
	void *op;  // the verification, asked on the main thread
	HRESULT hr;
	HANDLE asked;
} verify;

static void fail(asuka_call *call, int declared) {
	asuka_error e = {0};
	asuka_error_declared(&e, (uint32_t)declared);
	asuka_call_fail(call, &e);
}

// ask shows the prompt over the main window (main thread: its window's).
static void ask(void *p) {
	verify *v = p;
	void *interop = NULL;
	v->hr = asuka_winrt_factory("Windows.Security.Credentials.UI.UserConsentVerifier", &iid_interop, &interop);
	if (SUCCEEDED(v->hr)) {
		HSTRING msg = asuka_hstring(v->reason);
		GUID iid = asuka_winrt_iid("pinterface(" ASUKA_WINRT_ASYNC_OPERATION ";" VERIFICATION ")");
		// RequestVerificationForWindowAsync(HWND, HSTRING, REFIID, void **).
		v->hr = ASUKA_SLOT(interop, 6, HRESULT(STDMETHODCALLTYPE *)(void *, HWND, HSTRING, const GUID *, void **))(
		    interop, (HWND)asuka_desktop_window(0), msg, &iid, &v->op);
		asuka_hstring_free(msg);
		asuka_com_release(interop);
	}
	SetEvent(v->asked);
}

static void auth_run(void *p) {
	verify *v = p;
	asuka_winrt();
	if ((v->no_passcode && sensor() == 0) || availability(v->call) != available) {
		fail(v->call, biometric_BiometricError_notAvailable);
	} else {
		v->asked = CreateEventW(NULL, TRUE, FALSE, NULL);
		asuka_desktop_post(ask, v);
		WaitForSingleObject(v->asked, INFINITE);
		CloseHandle(v->asked);
		int r = canceled;
		if (SUCCEEDED(v->hr) && v->op) {
			GUID h = asuka_winrt_iid("pinterface(" ASUKA_WINRT_ASYNC_HANDLER ";" VERIFICATION ")");
			HRESULT hr = asuka_winrt_wait(v->op, &h, v->call);
			if (hr == S_OK) ASUKA_SLOT(v->op, 8, HRESULT(STDMETHODCALLTYPE *)(void *, int *))(v->op, &r);
			else if (hr != E_ABORT) r = no_device;
			asuka_com_release(v->op);
		} else {
			r = no_device;
		}
		switch (r) {
		case verified: biometric_Authenticator_authenticate_done(v->call); break;
		case canceled: fail(v->call, biometric_BiometricError_canceled); break;
		case no_device:
		case not_configured:
		case disabled:
		case retries:
		case busy: fail(v->call, biometric_BiometricError_notAvailable); break;
		default: fail(v->call, biometric_BiometricError_failed);
		}
	}
	free(v->reason);
	free(v);
}

static void b_authenticate(biometric_Authenticator *self, const char *reason, const char *cancel, bool no_passcode, asuka_call *call) {
	verify *v = calloc(1, sizeof *v);
	v->reason = asuka_strdup(reason && *reason ? reason : asuka_desktop_app_name());
	v->no_passcode = no_passcode;
	v->call = call;
	spawn(auth_run, v, call);
}

static const biometric_Authenticator_vtable vt = {.check = b_check, .authenticate = b_authenticate};

biometric_Authenticator *biometric_make_Authenticator(asuka_error *err) {
	biometric_Authenticator *a = malloc(sizeof *a);
	a->vt = &vt;
	return a;
}
