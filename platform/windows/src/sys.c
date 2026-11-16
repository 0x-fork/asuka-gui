// sys.c: what the system says and is asked, as internal/windows' win32.go
// had it: the registry's settings (dark mode, the accent, the text size),
// uxtheme's dark mode, DPI awareness, the common controls' activation
// context, the build, the user's languages, formats and time zone.
#include "host.h"
#include "parts.h"

bool wh_reg_dword(HKEY root, const wchar_t *path, const wchar_t *name, DWORD *out) {
	DWORD size = sizeof *out, type;
	return RegGetValueW(root, path, name, RRF_RT_REG_DWORD, &type, out, &size) == ERROR_SUCCESS;
}

bool wh_reg_binary(HKEY root, const wchar_t *path, const wchar_t *name, void *out, DWORD *size) {
	return RegGetValueW(root, path, name, RRF_RT_REG_BINARY, NULL, out, size) == ERROR_SUCCESS;
}

wchar_t *wh_reg_string(HKEY root, const wchar_t *path, const wchar_t *name) {
	DWORD size = 0;
	if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, NULL, NULL, &size) != ERROR_SUCCESS || size == 0) return NULL;
	wchar_t *s = wh_alloc(size + sizeof(wchar_t));
	if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, NULL, s, &size) != ERROR_SUCCESS) {
		free(s);
		return NULL;
	}
	return s;
}

bool wh_apps_use_light_theme(void) {
	DWORD v;
	return !wh_reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", &v) || v != 0;
}

bool wh_accent_palette(uint32_t out[7]) {
	uint8_t b[32];
	DWORD size = sizeof b;
	if (!wh_reg_binary(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette", b, &size) || size < 28)
		return false;
	for (int i = 0; i < 7; i++) out[i] = (uint32_t)b[i * 4] | (uint32_t)b[i * 4 + 1] << 8 | (uint32_t)b[i * 4 + 2] << 16;
	return true;
}

float wh_text_scale(void) {
	DWORD v;
	if (!wh_reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", L"TextScaleFactor", &v) || v < 100) return 1;
	return (float)v / 100;
}

// ---- uxtheme's dark mode (its undocumented ordinals, as Notepad++,
// MyGo and Windows Terminal use them) ----

static FARPROC preferred_mode, allow_dark_window, flush_menus;

static void dark_procs(void) {
	static bool done;
	if (done) return;
	done = true;
	HMODULE ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!ux || wh_build() < 17763) return;
	preferred_mode = GetProcAddress(ux, MAKEINTRESOURCEA(135));
	allow_dark_window = GetProcAddress(ux, MAKEINTRESOURCEA(133));
	flush_menus = GetProcAddress(ux, MAKEINTRESOURCEA(136));
}

void wh_set_app_dark(bool dark) {
	dark_procs();
	if (!preferred_mode) return;
	((int(WINAPI *)(int))(void *)preferred_mode)(dark ? 2 : 3);  // ForceDark, ForceLight
	if (flush_menus) ((void(WINAPI *)(void))(void *)flush_menus)();
}

void wh_allow_dark(HWND hwnd, bool dark) {
	dark_procs();
	if (allow_dark_window) ((BOOL(WINAPI *)(HWND, BOOL))(void *)allow_dark_window)(hwnd, dark);
}

void wh_set_dark_title_bar(HWND hwnd, bool dark) {
	BOOL v = dark;
	DwmSetWindowAttribute(hwnd, 20, &v, sizeof v);  // DWMWA_USE_IMMERSIVE_DARK_MODE
}

// ---- The system ----

DWORD wh_build(void) {
	static DWORD build;
	if (build) return build;
	typedef LONG(WINAPI * rtl_get_version)(OSVERSIONINFOW *);
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	rtl_get_version f = nt ? (rtl_get_version)(void *)GetProcAddress(nt, "RtlGetVersion") : NULL;
	OSVERSIONINFOW v = {sizeof v};
	if (f && f(&v) == 0) build = v.dwBuildNumber;
	if (!build) build = 1;
	return build;
}

bool wh_windows11(void) { return wh_build() >= 22000; }

void wh_dpi_aware(void) {
	typedef BOOL(WINAPI * set_ctx)(DPI_AWARENESS_CONTEXT);
	HMODULE u = GetModuleHandleW(L"user32.dll");
	set_ctx f = u ? (set_ctx)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
	if (f) f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

int wh_dpi(HWND hwnd) {
	UINT d = hwnd ? GetDpiForWindow(hwnd) : 0;
	if (!d) d = GetDpiForSystem();
	return d ? (int)d : 96;
}

void wh_adjust_window_rect(RECT *r, DWORD style, DWORD ex, int dpi) { AdjustWindowRectExForDpi(r, style, FALSE, ex, (UINT)dpi); }

int wh_system_metric(int i, int dpi) { return GetSystemMetricsForDpi(i, (UINT)dpi); }

// wh_activate_common_controls activates the common controls v6 for the
// process, from shell32's manifest (resource 124), so a host with no
// manifest of its own (the dev player, a test) draws them themed. A
// release's .exe has its own manifest too.
void wh_activate_common_controls(void) {
	wchar_t sys[MAX_PATH], src[MAX_PATH + 16];
	UINT n = GetSystemDirectoryW(sys, MAX_PATH);
	if (!n || n >= MAX_PATH) return;
	swprintf(src, WH_LEN(src), L"%ls\\shell32.dll", sys);
	ACTCTXW ctx = {sizeof ctx};
	ctx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID;
	ctx.lpSource = src;
	ctx.lpAssemblyDirectory = sys;
	ctx.lpResourceName = MAKEINTRESOURCEW(124);
	HANDLE h = CreateActCtxW(&ctx);
	if (h == INVALID_HANDLE_VALUE) return;
	ULONG_PTR cookie;
	ActivateActCtx(h, &cookie);
	INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_WIN95_CLASSES | ICC_DATE_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&icc);
}

// ---- Languages, formats, time ----

// wh_locale_name is the user's regional formats as the environment has
// them (BCP 47 with _: en_US).
void wh_locale_name(char *out, size_t cap) {
	wchar_t w[LOCALE_NAME_MAX_LENGTH];
	out[0] = 0;
	if (!GetUserDefaultLocaleName(w, LOCALE_NAME_MAX_LENGTH)) return;
	for (wchar_t *p = w; *p; p++)
		if (*p == '-') *p = '_';
	WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
}

// wh_preferred_languages is the user's display languages, most preferred
// first, joined by commas.
void wh_preferred_languages(char *out, size_t cap) {
	ULONG count = 0, size = 0;
	out[0] = 0;
	if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, NULL, &size) || size == 0) return;
	wchar_t *buf = wh_alloc(size * sizeof *buf);
	if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buf, &size)) {
		size_t at = 0;
		for (wchar_t *p = buf; *p; p += wcslen(p) + 1) {
			char tag[LOCALE_NAME_MAX_LENGTH * 3];
			int n = WideCharToMultiByte(CP_UTF8, 0, p, -1, tag, sizeof tag, NULL, NULL);
			if (n <= 1 || at + (size_t)n + 1 >= cap) break;
			if (at) out[at++] = ',';
			memcpy(out + at, tag, (size_t)n - 1);
			at += (size_t)n - 1;
			out[at] = 0;
		}
	}
	free(buf);
}

wchar_t *wh_locale_info(LCTYPE t) {
	int n = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, t, NULL, 0);
	wchar_t *s = wh_alloc(((size_t)(n > 0 ? n : 0) + 1) * sizeof *s);
	if (n > 0) GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, t, s, n);
	return s;
}

uint8_t wh_first_weekday(void) {
	wchar_t *s = wh_locale_info(LOCALE_IFIRSTDAYOFWEEK);
	int d = s[0] >= '0' && s[0] <= '6' && !s[1] ? s[0] - '0' : -1;
	free(s);
	return d < 0 ? 0 : (uint8_t)((d + 1) % 7 + 1);  // Windows' 0 is Monday
}

uint8_t wh_hour_cycle(void) {
	wchar_t *f = wh_locale_info(LOCALE_STIMEFORMAT);
	uint8_t c = 0;
	bool quoted = false;
	for (wchar_t *p = f; *p && !c; p++) {
		if (*p == '\'') quoted = !quoted;
		else if (!quoted && *p == 'h') c = 12;
		else if (!quoted && *p == 'H') c = 24;
	}
	free(f);
	if (c) return c;
	wchar_t *t = wh_locale_info(LOCALE_ITIME);
	if (t[0] == '0') c = 12;
	else if (t[0] == '1') c = 24;
	free(t);
	return c;
}

// wh_time_zone is the user's time zone's IANA name, by Windows's ICU.
void wh_time_zone(char *out, size_t cap) {
	out[0] = 0;
	DYNAMIC_TIME_ZONE_INFORMATION tz;
	if (GetDynamicTimeZoneInformation(&tz) == TIME_ZONE_ID_INVALID) return;
	HMODULE icu = LoadLibraryExW(L"icu.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!icu) return;
	typedef uint16_t UChar;
	typedef int32_t(__cdecl * conv)(const UChar *, int32_t, const char *, UChar *, int32_t, int *);
	conv f = (conv)(void *)GetProcAddress(icu, "ucal_getTimeZoneIDForWindowsID");
	if (!f) return;
	UChar id[64];
	int status = 0;
	int32_t n = f((const UChar *)tz.TimeZoneKeyName, (int32_t)wcslen(tz.TimeZoneKeyName), NULL, id, 64, &status);
	if (status > 0 || n <= 0 || n > 64) return;
	int len = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t *)id, n, out, (int)cap - 1, NULL, NULL);
	out[len > 0 ? len : 0] = 0;
}

// wh_set_thread_language makes tag the language this thread's system
// words are in, where Windows has it.
bool wh_set_thread_language(const char *tag) {
	if (!tag || !*tag) return false;
	wchar_t list[LOCALE_NAME_MAX_LENGTH + 2] = {0};
	MultiByteToWideChar(CP_UTF8, 0, tag, -1, list, LOCALE_NAME_MAX_LENGTH);
	ULONG n = 0;
	return SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, list, &n) != 0;
}

bool wh_key_down(int vk) { return GetKeyState(vk) < 0; }

int64_t wh_now_ns(void) {
	static LARGE_INTEGER f;
	if (!f.QuadPart) QueryPerformanceFrequency(&f);
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (int64_t)((double)t.QuadPart * 1e9 / (double)f.QuadPart);
}

double wh_seconds(void) { return (double)wh_now_ns() / 1e9; }
