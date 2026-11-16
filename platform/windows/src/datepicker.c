// datepicker.c: a DatePicker (host/windows' datepicker.go) is the date
// and time picker control (SysDateTimePick32): the locale's short date,
// its time, or both, its calendar dropping down. Inline
// (DatePickerProps.Inline), a date is a month calendar in place
// (SysMonthCal32, as WinUI's CalendarView), with a time picker under it
// for a date and time; a time alone stays the time picker, Windows having
// no clock to show in place.
#include "host.h"
#include "parts.h"

#define TIME_UNDER_GAP 8  // points between the calendar and the time

static bool is_calendar(HWND ctl) {
	wchar_t cls[32];
	return ctl && GetClassNameW(ctl, cls, (int)WH_LEN(cls)) && !wcscmp(cls, MONTHCAL_CLASSW);
}

// ---- Times: the value's milliseconds since 1970, the controls' local
// SYSTEMTIMEs ----

#define EPOCH_MS 11644473600000LL  // 1601 to 1970

static SYSTEMTIME system_time(int64_t ms) {
	ULARGE_INTEGER u;
	u.QuadPart = (ULONGLONG)(ms + EPOCH_MS) * 10000;
	FILETIME ft = {u.LowPart, u.HighPart};
	SYSTEMTIME utc, local;
	FileTimeToSystemTime(&ft, &utc);
	if (!SystemTimeToTzSpecificLocalTime(NULL, &utc, &local)) local = utc;
	return local;
}

static int64_t from_system_time(const SYSTEMTIME *local) {
	SYSTEMTIME utc;
	if (!TzSpecificLocalTimeToSystemTime(NULL, local, &utc)) utc = *local;
	FILETIME ft;
	SystemTimeToFileTime(&utc, &ft);
	ULARGE_INTEGER u = {.LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime};
	return (int64_t)(u.QuadPart / 10000) - EPOCH_MS;
}

static int64_t now_ms(void) {
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	ULARGE_INTEGER u = {.LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime};
	return (int64_t)(u.QuadPart / 10000) - EPOCH_MS;
}

// ---- Making ----

// date_format sets a date picker's format: the locale's short date, its
// time without seconds, or both.
static void date_format(node *n) {
	wchar_t *date = wh_locale_info(LOCALE_SSHORTDATE), *tm = wh_locale_info(LOCALE_SSHORTTIME);
	wchar_t f[256];
	switch (n->date_mode) {
	case AK_DATE_MODE_TIME: swprintf(f, WH_LEN(f), L"%ls", tm); break;
	case AK_DATE_MODE_DATE_TIME: swprintf(f, WH_LEN(f), L"%ls %ls", date, tm); break;
	default: swprintf(f, WH_LEN(f), L"%ls", date);
	}
	SendMessageW(n->ctl, DTM_SETFORMATW, 0, (LPARAM)f);
	free(date);
	free(tm);
}

static void theme_field(node *n, HWND dtp);
static void apply_date(node *n);
static void apply_range(node *n);

// time_under makes or drops the time picker under a calendar: one for a
// date and time.
static void time_under(node *n) {
	bool want = n->date_mode == AK_DATE_MODE_DATE_TIME;
	if (want && !n->ctl2) {
		n->ctl2 = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | DTS_TIMEFORMAT, 0, 0, 0, 0, n->hwnd,
			(HMENU)2, GetModuleHandleW(NULL), NULL);
		SendMessageW(n->ctl2, WM_SETFONT, (WPARAM)wh_node_font(n), 0);
		SendMessageW(n->ctl2, WM_UPDATEUISTATE, MAKELONG(UIS_SET, UISF_HIDEFOCUS), 0);
		wchar_t *tm = wh_locale_info(LOCALE_SSHORTTIME);
		SendMessageW(n->ctl2, DTM_SETFORMATW, 0, (LPARAM)tm);
		free(tm);
		EnableWindow(n->ctl2, !n->disabled);
		theme_field(n, n->ctl2);
		wh_bind(n, n->ctl2);
		apply_date(n);
		wh_layout_field(n);
	} else if (!want && n->ctl2) {
		wh_unsubclass(n->ctl2);
		wh_unbind(n->ctl2);
		DestroyWindow(n->ctl2);
		n->ctl2 = NULL;
		wh_layout_field(n);
	}
}

static void drop_controls(node *n) {
	HWND cs[2] = {n->ctl, n->ctl2};
	for (int i = 0; i < 2; i++)
		if (cs[i]) {
			wh_unsubclass(cs[i]);
			wh_unbind(cs[i]);
			DestroyWindow(cs[i]);
		}
	n->ctl = n->ctl2 = NULL;
}

// wh_make_date_controls makes n's controls for its mode and Inline, again
// when they change.
void wh_make_date_controls(node *n) {
	bool calendar = n->date_inline && n->date_mode != AK_DATE_MODE_TIME;
	if (n->ctl) {
		if (is_calendar(n->ctl) == calendar) {
			if (!calendar) date_format(n);
			else time_under(n);
			return;
		}
		drop_controls(n);
	}
	if (calendar) {
		wh_make_control_in(n, MONTHCAL_CLASSW, WS_TABSTOP | MCS_NOTODAY, 0);
		wh_bind(n, n->ctl);
		time_under(n);
	} else {
		wh_make_control_in(n, DATETIMEPICK_CLASSW, WS_TABSTOP | DTS_SHORTDATEFORMAT, 0);
		wh_bind(n, n->ctl);
		date_format(n);
	}
	if (n->disabled) EnableWindow(n->ctl, FALSE);
	wh_theme_control(n);
	apply_range(n);
	apply_date(n);
	wh_layout_field(n);
}

// apply_date shows n's value in its controls.
static void apply_date(node *n) {
	if (!n->ctl) return;
	int64_t ms = (int64_t)n->value;
	if (!ms) ms = now_ms();
	SYSTEMTIME st = system_time(ms);
	if (n->ctl2 || is_calendar(n->ctl)) {
		SendMessageW(n->ctl, MCM_SETCURSEL, 0, (LPARAM)&st);
		if (n->ctl2) SendMessageW(n->ctl2, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
		return;
	}
	SendMessageW(n->ctl, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
}

// apply_range bounds n's controls' choice by Min and Max.
static void apply_range(node *n) {
	SYSTEMTIME r[2] = {0};
	WPARAM flags = 0;
	if (n->min != 0) r[0] = system_time((int64_t)n->min), flags |= GDTR_MIN;
	if (n->max != 0) r[1] = system_time((int64_t)n->max), flags |= GDTR_MAX;
	if (!n->ctl) return;
	if (is_calendar(n->ctl)) {
		SendMessageW(n->ctl, MCM_SETRANGE, flags, (LPARAM)r);
		if (n->ctl2) SendMessageW(n->ctl2, DTM_SETRANGE, flags, (LPARAM)r);
	} else {
		SendMessageW(n->ctl, DTM_SETRANGE, flags, (LPARAM)r);
	}
}

void wh_set_date_prop(node *n, uint16_t p, const ak_value *v) {
	switch (p) {
	case AK_PROP_DATE_PICKER_VALUE:
		n->value = (double)v->i;
		apply_date(n);
		break;
	case AK_PROP_DATE_PICKER_MODE:
		n->date_mode = (int32_t)v->i;
		wh_make_date_controls(n);
		break;
	case AK_PROP_DATE_PICKER_INLINE:
		n->date_inline = v->i != 0;
		wh_make_date_controls(n);
		break;
	case AK_PROP_DATE_PICKER_MIN:
	case AK_PROP_DATE_PICKER_MAX:
		if (p == AK_PROP_DATE_PICKER_MIN) n->min = (double)v->i;
		else n->max = (double)v->i;
		apply_range(n);
		break;
	}
}

// picked reports a choice in one of n's controls: a calendar's day keeps
// the value's time of day, a time picker's time its day.
static void picked(node *n, HWND from, SYSTEMTIME st) {
	if (n->date_inline && n->ctl && from) {
		SYSTEMTIME cur = system_time(n->value != 0 ? (int64_t)n->value : now_ms());
		if (from == n->ctl && is_calendar(n->ctl)) {
			// The calendar: its day, the value's time.
			st.wHour = cur.wHour, st.wMinute = cur.wMinute, st.wSecond = cur.wSecond, st.wMilliseconds = 0;
		} else if (from == n->ctl2) {
			// The time under it: its time, the value's day.
			st.wYear = cur.wYear, st.wMonth = cur.wMonth, st.wDay = cur.wDay, st.wDayOfWeek = cur.wDayOfWeek;
			st.wMilliseconds = 0;
		}
	}
	int64_t ms = from_system_time(&st);
	if ((double)ms == n->value) return;  // a calendar moving to another month keeps its selection
	n->value = (double)ms;
	wh_emit_float(n, AK_EVENT_VALUE_CHANGED, (double)ms);
}

// ---- Measuring and placing ----

// wh_measure_date is n's size in points: the picker's ideal size; a
// calendar's least, with the time picker under it.
void wh_measure_date(node *n, float *w, float *h) {
	*w = 100, *h = 32;
	if (!n->ctl) return;
	if (is_calendar(n->ctl)) {
		RECT r = {0};
		SendMessageW(n->ctl, MCM_GETMINREQRECT, 0, (LPARAM)&r);
		*w = wh_pt(n, r.right - r.left), *h = wh_pt(n, r.bottom - r.top);
		if (n->ctl2) *h += TIME_UNDER_GAP + 32;
		return;
	}
	SIZE sz = {0};
	SendMessageW(n->ctl, DTM_GETIDEALSIZE, 0, (LPARAM)&sz);
	*w = wh_maxf(wh_pt(n, sz.cx) + 8, 100), *h = wh_maxf(wh_pt(n, sz.cy), 32);
}

// wh_layout_date places a picker's controls: the picker filling its view,
// or the calendar on top and the time under it, at its leading edge.
void wh_layout_date(node *n, RECT r) {
	if (!n->ctl) return;
	int w = r.right - r.left, h = r.bottom - r.top;
	if (!n->ctl2) {
		SetWindowPos(n->ctl, NULL, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
		return;
	}
	int th = wh_px(n, 32);
	int ch = wh_maxi(h - th - wh_px(n, TIME_UNDER_GAP), 1);
	SetWindowPos(n->ctl, NULL, 0, 0, w, ch, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
	int tw = wh_mini(w, wh_px(n, 160));
	int x = wh_is_rtl(n) ? w - tw : 0;
	SetWindowPos(n->ctl2, NULL, x, h - th, tw, th, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

// ---- Dark mode ----

// theme_calendar colors a month calendar for the app's dark mode: a month
// calendar has no dark visual style, so dark, it's drawn classic in the
// dark palette's colors (Fluent's layer and text, the title and the days'
// names in the accent's); light, in its visual style.
static void theme_calendar(HWND cal) {
	wh_allow_dark(cal, H.dark);
	if (!H.dark) {
		SetWindowTheme(cal, NULL, NULL);
		InvalidateRect(cal, NULL, FALSE);
		return;
	}
	SetWindowTheme(cal, L"", L"");
	const palette *p = &H.theme;
	struct {
		int part;
		rgba c;
	} colors[] = {
		{MCSC_BACKGROUND, rgba_over(p->layer, p->bg)}, {MCSC_MONTHBK, rgba_over(p->layer, p->bg)}, {MCSC_TEXT, p->label},
		{MCSC_TITLEBK, p->accent}, {MCSC_TITLETEXT, p->on_accent}, {MCSC_TRAILINGTEXT, rgba_over(p->tertiary, p->bg)},
	};
	for (size_t i = 0; i < WH_LEN(colors); i++) SendMessageW(cal, MCM_SETCOLOR, (WPARAM)colors[i].part, (LPARAM)rgba_ref(colors[i].c));
	InvalidateRect(cal, NULL, FALSE);
}

// dark_field paints a date and time picker as it draws itself, then turns
// it dark: white is the dark field's background, black its text, what's
// between mixed. The selected part's highlight stays.
static void dark_field(HWND hwnd) {
	PAINTSTRUCT ps;
	HDC dc = BeginPaint(hwnd, &ps);
	RECT r;
	GetClientRect(hwnd, &r);
	if (r.right <= 0 || r.bottom <= 0) {
		EndPaint(hwnd, &ps);
		return;
	}
	HDC mem = CreateCompatibleDC(dc);
	BITMAPINFO bi = {{sizeof bi.bmiHeader, r.right, -r.bottom, 1, 32, BI_RGB, 0, 0, 0, 0, 0}, {{0, 0, 0, 0}}};
	uint8_t *px = NULL;
	HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
	if (!bmp || !px) {
		if (bmp) DeleteObject(bmp);
		DeleteDC(mem);
		EndPaint(hwnd, &ps);
		return;
	}
	HGDIOBJ old = SelectObject(mem, bmp);
	wh_def_subclass(hwnd, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT | PRF_ERASEBKGND);
	GdiFlush();
	rgba bg = rgba_over(H.theme.field_bg, H.theme.bg), fg = H.theme.label;
	COLORREF hl = GetSysColor(COLOR_HIGHLIGHT);
	size_t count = (size_t)r.right * (size_t)r.bottom * 4;
	for (size_t i = 0; i + 3 < count; i += 4) {
		int b = px[i], g = px[i + 1], rd = px[i + 2];
		if (abs(b - (int)GetBValue(hl)) + abs(g - (int)GetGValue(hl)) + abs(rd - (int)GetRValue(hl)) < 60) continue;  // the selected part's highlight
		double t = 1 - (double)(b + g + rd) / (3 * 255);  // how much text
#define MIX(a, z) (uint8_t)((double)(a) + ((double)(z) - (double)(a)) * t + 0.5)
		px[i] = MIX(bg.b, fg.b), px[i + 1] = MIX(bg.g, fg.g), px[i + 2] = MIX(bg.r, fg.r);
#undef MIX
	}
	BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
	SelectObject(mem, old);
	DeleteObject(bmp);
	DeleteDC(mem);
	EndPaint(hwnd, &ps);
}

static LRESULT field_message(HWND hwnd, UINT m, WPARAM w, LPARAM l) {
	if (m == WM_PAINT && H.dark) {
		dark_field(hwnd);
		return 0;
	}
	return wh_def_subclass(hwnd, m, w, l);
}

// theme_field draws a date and time picker's field dark, while the app
// is, its text antialiased in grays (ClearType's colored edges don't turn
// dark).
static void theme_field(node *n, HWND dtp) {
	wh_allow_dark(dtp, H.dark);
	font_spec s = wh_node_spec(n);
	s.gray = H.dark;
	SendMessageW(dtp, WM_SETFONT, (WPARAM)wh_font(s), 0);
	wh_subclass(dtp, field_message);
	InvalidateRect(dtp, NULL, FALSE);
}

// wh_theme_date follows dark mode on n's controls. The date picker's
// drop-down calendar likewise (DTN_DROPDOWN). A date and time picker's
// field, which has no dark style either, is drawn light and turned dark:
// its grays become the dark field's, the text light on it (dark_field).
void wh_theme_date(node *n) {
	if (!n->ctl) return;
	if (is_calendar(n->ctl)) theme_calendar(n->ctl);
	else theme_field(n, n->ctl);
	if (n->ctl2) theme_field(n, n->ctl2);
}

// drop_down themes the calendar a date picker dropped down.
static void drop_down(HWND dtp) {
	HWND cal = (HWND)SendMessageW(dtp, DTM_GETMONTHCAL, 0, 0);
	if (!cal) return;
	theme_calendar(cal);
	if (H.dark) wh_set_dark_title_bar(GetParent(cal), true);
}

// wh_date_notify is a picker's controls' WM_NOTIFY: a choice, a calendar
// dropping down.
LRESULT wh_date_notify(node *n, NMHDR *hdr, LPARAM lp, bool *handled) {
	*handled = false;
	switch (hdr->code) {
	case DTN_DATETIMECHANGE: {
		NMDATETIMECHANGE *ch = (NMDATETIMECHANGE *)lp;
		if (ch->dwFlags == GDT_VALID) picked(n, hdr->hwndFrom, ch->st);
		*handled = true;
		return 0;
	}
	case DTN_DROPDOWN:
		drop_down(hdr->hwndFrom);
		*handled = true;
		return 0;
	case MCN_SELCHANGE:
		picked(n, hdr->hwndFrom, ((NMSELCHANGE *)lp)->stSelStart);
		*handled = true;
		return 0;
	}
	return 0;
}
