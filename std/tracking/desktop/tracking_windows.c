// asuka/tracking on Windows: Settings' "Let apps show me personalized ads
// by using my advertising ID": off, the ID is empty (denied). macOS runs
// the package's Swift; Linux has no such setting, which Asuka's side
// answers as allowed.
#include "asuka_windows.h"
#include "tracking.asuka.h"

#include <stdlib.h>

static const GUID iid_advertising_manager_statics = {0xADD3468C, 0xA273, 0x48CB, {0xB3, 0x46, 0x35, 0x44, 0x52, 0x2D, 0x55, 0x81}};

static tracking_Status status(void) {
	void *st = NULL;
	if (FAILED(asuka_winrt_factory("Windows.System.UserProfile.AdvertisingManager", &iid_advertising_manager_statics, &st)))
		return tracking_Status_allowed;  // no setting: as Android
	HSTRING id = NULL;
	tracking_Status s = tracking_Status_allowed;
	if (SUCCEEDED(ASUKA_SLOT(st, 6, HRESULT(STDMETHODCALLTYPE *)(void *, HSTRING *))(st, &id))) {  // get_AdvertisingId
		char *v = asuka_hstring_utf8(id);
		if (!*v) s = tracking_Status_denied;
		free(v);
		asuka_hstring_free(id);
	}
	asuka_com_release(st);
	return s;
}

static tracking_Status t_current(tracking_Tracker *self, asuka_error *err) { return status(); }
static void t_request(tracking_Tracker *self, asuka_call *call) { tracking_Tracker_request_done(call, status()); }

static const tracking_Tracker_vtable vt = {.current = t_current, .request = t_request};

tracking_Tracker *tracking_make_Tracker(asuka_error *err) {
	tracking_Tracker *t = malloc(sizeof *t);
	t->vt = &vt;
	return t;
}
