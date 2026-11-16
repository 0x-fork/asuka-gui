// time.c: asuka/time's builtins. A Time (std/time/time.asuka) is an
// instant (nanoseconds since the Unix epoch), its zone, the zone's
// offset at that instant, and the wall clock's fields, which the Asuka
// side computes from the first three. What it needs from C:
//
//	time.wallNanos() -> int            now; with the manual clock (tests),
//	                                   2026-01-01T00:00:00Z plus its time
//	time.monotonic() -> Duration       (rt/lib.c's)
//	time.localOffset(unix) -> int      the local zone's offset from UTC at
//	                                   that second, in seconds east (UTC in
//	                                   tests, so they see the same dates)
//	time.formatFields(pattern, …) -> string
//	time.parseRFC3339(s) -> [instant, offset]   throws
#define _POSIX_C_SOURCE 200809L  // localtime_r under -std=c17
#include "std/std.h"
#include "rt/sched.h"
#include "base/thread.h"

#include <stdio.h>
#include <time.h>

#define STR(i) ((ak_str *)args[i].p)

// days_from_civil is the number of days from 1970-01-01 to y-m-d in the
// proleptic Gregorian calendar (Howard Hinnant's algorithm).
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d) {
	y -= m <= 2;
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	int64_t yoe = y - era * 400;
	int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

// ---- The clock and the zone ----

static ak_val wall_nanos(ak_val *args, uint32_t n) {
	AK_UNUSED(args), AK_UNUSED(n);
	return (ak_val){.i = ak_sched_wall()};
}

#if defined(__wasm__)
// The browser's zone (platform/web/src/core.js): wasi-libc has none.
__attribute__((import_module("asuka"), import_name("tz_offset"))) double js_tz_offset(double unix);
#endif

static int64_t local_offset(int64_t t) {
	if (ak_sched_manual()) return 0;
#if defined(__wasm__)
	return (int64_t)js_tz_offset((double)t);
#else
	time_t tt = (time_t)t;
	struct tm tm;
#if defined(_WIN32)
	if (localtime_s(&tm, &tt) != 0) return 0;
#else
	if (!localtime_r(&tt, &tm)) return 0;
#endif
	int64_t local = days_from_civil((int64_t)tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 86400 + tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
	return local - t;
#endif
}

static ak_val local_offset_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.i = local_offset(args[0].i)};
}

// ---- Formatting ----
//
// A subset of Unicode's date patterns (UTS #35), in English: letters are
// fields, repeated for width or form; text in '…' is literal, '' a quote;
// anything else is itself.

static const char *const month_names[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
static const char *const day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

static void num(ak_buf *b, int64_t v, int width) {
	if (v < 0) {
		ak_buf_u8(b, '-');
		v = -v;
	}
	ak_buf_printf(b, "%0*lld", width, (long long)v);
}

static void name(ak_buf *b, const char *s, int count) {
	size_t len = strlen(s);
	if (count == 3) len = 3;          // Jan, Mon
	else if (count == 5) len = 1;     // J, M
	else if (count == 6) len = 2;     // Mo (days)
	ak_buf_put(b, s, len);
}

static void zone(ak_buf *b, int64_t off, bool colon, bool minutes, bool z) {
	if (off == 0 && z) {
		ak_buf_u8(b, 'Z');
		return;
	}
	ak_buf_u8(b, off < 0 ? '-' : '+');
	if (off < 0) off = -off;
	int64_t h = off / 3600, m = off / 60 % 60;
	ak_buf_printf(b, "%02lld", (long long)h);
	if (minutes || m) ak_buf_printf(b, colon ? ":%02lld" : "%02lld", (long long)m);
}

typedef struct {
	int64_t year, month, day, hour, minute, second, nano, weekday, offset;
} fields;

static void field(ak_buf *b, char c, int count, const fields *f) {
	switch (c) {
	case 'G':
		ak_buf_put(b, f->year > 0 ? "AD" : "BC", 2);
		break;
	case 'y':
	case 'u':
		if (count == 2) num(b, (f->year % 100 + 100) % 100, 2);
		else num(b, f->year, count);
		break;
	case 'M':
	case 'L':
		if (count <= 2) num(b, f->month, count);
		else name(b, month_names[(f->month - 1) % 12], count);
		break;
	case 'd':
		num(b, f->day, count);
		break;
	case 'D':
		num(b, days_from_civil(f->year, f->month, f->day) - days_from_civil(f->year, 1, 1) + 1, count);
		break;
	case 'E':
	case 'c':
	case 'e':
		if ((c == 'e' || c == 'c') && count <= 2) num(b, f->weekday + 1, count);
		else name(b, day_names[f->weekday % 7], count < 3 ? 3 : count);
		break;
	case 'a':
		ak_buf_put(b, f->hour < 12 ? "AM" : "PM", 2);
		break;
	case 'h':
		num(b, f->hour % 12 == 0 ? 12 : f->hour % 12, count);
		break;
	case 'H':
		num(b, f->hour, count);
		break;
	case 'K':
		num(b, f->hour % 12, count);
		break;
	case 'k':
		num(b, f->hour == 0 ? 24 : f->hour, count);
		break;
	case 'm':
		num(b, f->minute, count);
		break;
	case 's':
		num(b, f->second, count);
		break;
	case 'S': {
		// Fractions of a second, truncated to count digits.
		char d[10];
		snprintf(d, sizeof d, "%09lld", (long long)f->nano);
		for (int i = 0; i < count; i++) ak_buf_u8(b, i < 9 ? (uint8_t)d[i] : '0');
		break;
	}
	case 'Z':
		if (count == 4) {
			ak_buf_put(b, "GMT", 3);
			if (f->offset) zone(b, f->offset, true, true, false);
		} else {
			zone(b, f->offset, count == 5, true, count == 5);
		}
		break;
	case 'X':
	case 'x':
		zone(b, f->offset, count >= 3, count >= 2, c == 'X');
		break;
	default:
		for (int i = 0; i < count; i++) ak_buf_u8(b, (uint8_t)c);
	}
}

static ak_str *format(const ak_str *p, const fields *f) {
	ak_buf b = {0};
	for (uint32_t i = 0; i < p->len;) {
		char c = p->data[i];
		if (c == '\'') {
			if (i + 1 < p->len && p->data[i + 1] == '\'') {
				ak_buf_u8(&b, '\'');
				i += 2;
				continue;
			}
			// Quoted text, '' inside it a quote.
			i++;
			while (i < p->len) {
				if (p->data[i] == '\'') {
					if (i + 1 < p->len && p->data[i + 1] == '\'') {
						ak_buf_u8(&b, '\'');
						i += 2;
						continue;
					}
					i++;
					break;
				}
				ak_buf_u8(&b, (uint8_t)p->data[i++]);
			}
			continue;
		}
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
			int count = 0;
			while (i < p->len && p->data[i] == c) i++, count++;
			field(&b, c, count, f);
			continue;
		}
		ak_buf_u8(&b, (uint8_t)c);
		i++;
	}
	ak_str *s = ak_str_new((const char *)b.data, b.len);
	ak_buf_free(&b);
	return s;
}

static ak_val format_fields(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	fields f = {args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i};
	return (ak_val){.p = format(STR(0), &f)};
}

// ---- RFC 3339 ----

static bool digits(const char *s, size_t len, size_t *i, int n, int64_t *out) {
	int64_t v = 0;
	for (int k = 0; k < n; k++) {
		if (*i >= len || s[*i] < '0' || s[*i] > '9') return false;
		v = v * 10 + (s[(*i)++] - '0');
	}
	*out = v;
	return true;
}

static bool leap(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int64_t month_days(int64_t y, int64_t m) {
	static const int8_t d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	return m == 2 && leap(y) ? 29 : d[m - 1];
}

// parse_rfc3339 reads 2006-01-02T15:04:05.999999999Z07:00 (a t or a space
// for the T, z for Z, the fraction optional), from 1678 to 2261.
static bool parse_rfc3339(const ak_str *str, int64_t *instant, int64_t *offset) {
	const char *s = str->data;
	size_t len = str->len, i = 0;
	int64_t y, mo, d, h, mi, sec, nano = 0, off = 0;
	if (!digits(s, len, &i, 4, &y) || i >= len || s[i++] != '-') return false;
	if (!digits(s, len, &i, 2, &mo) || i >= len || s[i++] != '-') return false;
	if (!digits(s, len, &i, 2, &d) || i >= len) return false;
	char t = s[i++];
	if (t != 'T' && t != 't' && t != ' ') return false;
	if (!digits(s, len, &i, 2, &h) || i >= len || s[i++] != ':') return false;
	if (!digits(s, len, &i, 2, &mi) || i >= len || s[i++] != ':') return false;
	if (!digits(s, len, &i, 2, &sec)) return false;
	if (i < len && s[i] == '.') {
		i++;
		int k = 0;
		int64_t scale = 100000000;
		while (i < len && s[i] >= '0' && s[i] <= '9') {
			nano += (s[i++] - '0') * scale;
			scale /= 10;
			k++;
		}
		if (k == 0) return false;
	}
	if (i >= len) return false;
	if (s[i] == 'Z' || s[i] == 'z') {
		i++;
	} else if (s[i] == '+' || s[i] == '-') {
		int sign = s[i++] == '-' ? -1 : 1;
		int64_t oh, om;
		if (!digits(s, len, &i, 2, &oh) || i >= len || s[i++] != ':' || !digits(s, len, &i, 2, &om)) return false;
		if (oh > 23 || om > 59) return false;
		off = sign * (oh * 3600 + om * 60);
	} else {
		return false;
	}
	if (i != len) return false;
	if (mo < 1 || mo > 12 || d < 1 || d > month_days(y, mo) || h > 23 || mi > 59 || sec > 59) return false;
	int64_t secs = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec - off;
	// An instant is an int of nanoseconds: 1677 to 2262.
	if (__builtin_mul_overflow(secs, 1000000000, instant) || __builtin_add_overflow(*instant, nano, instant)) return false;
	*offset = off;
	return true;
}

static ak_val parse_fn(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t instant, offset;
	if (!parse_rfc3339(STR(0), &instant, &offset)) {
		ak_throw_msg("time: %s isn't an RFC 3339 time from 1678 to 2261 (2006-01-02T15:04:05Z)", STR(0)->data);
		return (ak_val){0};
	}
	ak_arr *a = ak_arr_new(AK_K_INT, NULL, 2);
	a->data[0].i = instant;
	a->data[1].i = offset;
	a->len = 2;
	return (ak_val){.p = a};
}

const ak_lib_entry ak_std_time[] = {
	{"time.wallNanos", wall_nanos},
	{"time.localOffset", local_offset_fn},
	{"time.formatFields", format_fields},
	{"time.parseRFC3339", parse_fn},
	{NULL, NULL},
};
