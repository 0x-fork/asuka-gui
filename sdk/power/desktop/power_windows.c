// platform/power on Windows: the battery and the power source
// (GetSystemPowerStatus; low power is battery saver, and Windows 11's
// energy saver, which it reports the same way; Windows has no thermal
// state for apps: nominal), read again as WM_POWERBROADCAST says they
// changed. Sleep and waking are PBT_APMSUSPEND and PBT_APMRESUMEAUTOMATIC
// (or PBT_APMRESUMESUSPEND alone), which RegisterSuspendResumeNotification
// sends on PCs with modern standby too, reported once each; locking is
// the session's WM_WTSSESSION_CHANGE. The idle time is
// GetLastInputInfo's. All of it comes to the system window (main thread).
#include "asuka_windows.h"
#include "power.asuka.h"

#include <stdlib.h>
#include <string.h>

#ifndef WM_WTSSESSION_CHANGE
#define WM_WTSSESSION_CHANGE 0x02B1
#endif
enum { WTS_LOCK = 7, WTS_UNLOCK = 8 };

static power_Status state(void) {
	SYSTEM_POWER_STATUS p;
	power_Status s = {.level = -1};
	if (!GetSystemPowerStatus(&p)) return s;
	bool battery = p.BatteryFlag != 128 && p.BatteryFlag != 255;
	s.battery = battery;
	s.onBattery = battery && p.ACLineStatus == 0;
	s.charging = battery && (p.BatteryFlag & 8) != 0;
	s.lowPower = (p.SystemStatusFlag & 1) != 0;
	if (battery && p.BatteryLifePercent <= 100) s.level = p.BatteryLifePercent / 100.0;
	return s;
}

static power_Status m_current(power_Monitor *self, asuka_error *err) { return state(); }

// asleep: suspending was reported, waking not yet. Both the broadcast and
// RegisterSuspendResumeNotification's message can come.
static bool asleep;

typedef struct {
	power_fn_Status changed;
	power_fn_Event happened;
	bool watch;
	asuka_call *call;
	int id;
	power_Status last;
	bool first;
} following;

static void report(following *f) {
	power_Status s = state();
	if (!f->first && memcmp(&s, &f->last, sizeof s) == 0) return;
	f->first = false;
	f->last = s;
	power_fn_Status_call(f->changed, &s);
}

static void on_message(void *ctx, UINT m, WPARAM wp, LPARAM lp) {
	following *f = ctx;
	if (m == WM_POWERBROADCAST) {
		switch (wp) {
		case PBT_APMPOWERSTATUSCHANGE:
		case PBT_POWERSETTINGCHANGE:
			if (f->watch) report(f);
			break;
		case PBT_APMSUSPEND:
			if (!asleep) {
				asleep = true;
				if (!f->watch) power_fn_Event_call(f->happened, power_Event_suspend);
			}
			break;
		case PBT_APMRESUMEAUTOMATIC:
		case PBT_APMRESUMESUSPEND:
			if (asleep) {
				asleep = false;
				if (!f->watch) power_fn_Event_call(f->happened, power_Event_resume);
			}
			if (f->watch) report(f);  // unplugged while asleep
			break;
		}
	} else if (m == WM_WTSSESSION_CHANGE && !f->watch) {
		if (wp == WTS_LOCK) power_fn_Event_call(f->happened, power_Event_lock);
		else if (wp == WTS_UNLOCK) power_fn_Event_call(f->happened, power_Event_unlock);
	}
}

static void stop(void *p) {
	following *f = p;
	asuka_win_system_message_stop(f->id);
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(f->call, &e);
	free(f);
}

static void m_watch(power_Monitor *self, power_fn_Status changed, asuka_call *call) {
	following *f = calloc(1, sizeof *f);
	f->changed = changed;
	f->watch = true;
	f->call = call;
	f->first = true;
	f->id = asuka_win_system_message(on_message, f);
	report(f);
	asuka_call_on_cancel(call, stop, f);
}

static void m_events(power_Monitor *self, power_fn_Event happened, asuka_call *call) {
	following *f = calloc(1, sizeof *f);
	f->happened = happened;
	f->call = call;
	f->id = asuka_win_system_message(on_message, f);
	asuka_call_on_cancel(call, stop, f);
}

static void m_idle(power_Monitor *self, asuka_call *call) {
	LASTINPUTINFO in = {sizeof in, 0};
	if (!GetLastInputInfo(&in)) {
		asuka_error e = {0};
		asuka_error_declared(&e, power_PowerError_idleUnknown);
		asuka_call_fail(call, &e);
		return;
	}
	DWORD ms = GetTickCount() - in.dwTime;  // both wrap at 49.7 days
	power_Monitor_idle_done(call, (int64_t)ms * 1000000);
}

static const power_Monitor_vtable vt = {.current = m_current, .watch = m_watch, .events_ = m_events, .idle = m_idle};

power_Monitor *power_make_Monitor(asuka_error *err) {
	power_Monitor *m = malloc(sizeof *m);
	m->vt = &vt;
	return m;
}
