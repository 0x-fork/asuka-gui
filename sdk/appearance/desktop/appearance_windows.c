// platform/appearance on Windows: the accent color (Settings,
// Personalization, Colors: the accent palette's own entry, else DWM's
// colorization color), a contrast theme on (SPI_GETHIGHCONTRAST) as
// Increase Contrast, and transparency effects off (the Personalize key's
// EnableTransparency) as Reduce Transparency. They're read again as the
// system window hears WM_SETTINGCHANGE, WM_SYSCOLORCHANGE,
// WM_DWMCOLORIZATIONCOLORCHANGED or WM_THEMECHANGED (main thread).
#include "asuka_windows.h"
#include <dwmapi.h>
#include "appearance.asuka.h"

#include <stdlib.h>
#include <string.h>

#ifndef WM_DWMCOLORIZATIONCOLORCHANGED
#define WM_DWMCOLORIZATIONCOLORCHANGED 0x0320
#endif

static appearance_Settings settings(void) {
	appearance_Settings s = {0};
	uint8_t pal[32];
	DWORD n = sizeof pal;
	uint32_t c = 0;
	bool has = false;
	if (asuka_win_reg(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette", REG_BINARY, pal, &n) && n >= 28) {
		c = pal[12] | (uint32_t)pal[13] << 8 | (uint32_t)pal[14] << 16;  // 0x00BBGGRR, the palette's own entry (4th)
		has = true;
	} else {
		DWORD argb = 0;
		BOOL opaque = FALSE;
		HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
		HRESULT(WINAPI * get)(DWORD *, BOOL *) = NULL;
		if (dwm) *(FARPROC *)&get = GetProcAddress(dwm, "DwmGetColorizationColor");
		if (get && SUCCEEDED(get(&argb, &opaque))) {
			c = (argb >> 16 & 0xff) | (argb & 0xff00) | (argb & 0xff) << 16;
			has = true;
		}
	}
	if (has) {
		s.hasAccent = true;
		s.red = c & 0xff;
		s.green = c >> 8 & 0xff;
		s.blue = c >> 16 & 0xff;
	}
	HIGHCONTRASTW hc = {sizeof hc, 0, NULL};
	if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0)) s.increaseContrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
	DWORD v = 1;
	n = sizeof v;
	if (asuka_win_reg(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", REG_DWORD, &v, &n)) s.reduceTransparency = v == 0;
	return s;
}

static appearance_Settings s_current(appearance_System *self, asuka_error *err) { return settings(); }

typedef struct {
	appearance_fn_Settings changed;
	asuka_call *call;
	int id;
	appearance_Settings last;
	bool first;
} watching;

static void report(watching *w) {
	appearance_Settings s = settings();
	if (!w->first && memcmp(&s, &w->last, sizeof s) == 0) return;
	w->first = false;
	w->last = s;
	appearance_fn_Settings_call(w->changed, &s);
}

static void on_message(void *ctx, UINT m, WPARAM wp, LPARAM lp) {
	if (m == WM_SETTINGCHANGE || m == WM_SYSCOLORCHANGE || m == WM_DWMCOLORIZATIONCOLORCHANGED || m == WM_THEMECHANGED) report(ctx);
}

static void stop(void *p) {
	watching *w = p;
	asuka_win_system_message_stop(w->id);
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(w->call, &e);
	free(w);
}

static void s_watch(appearance_System *self, appearance_fn_Settings changed, asuka_call *call) {
	watching *w = calloc(1, sizeof *w);
	w->changed = changed;
	w->call = call;
	w->first = true;
	w->id = asuka_win_system_message(on_message, w);
	report(w);
	asuka_call_on_cancel(call, stop, w);
}

static const appearance_System_vtable vt = {.current = s_current, .watch = s_watch};

appearance_System *appearance_make_System(asuka_error *err) {
	appearance_System *s = malloc(sizeof *s);
	s->vt = &vt;
	return s;
}
