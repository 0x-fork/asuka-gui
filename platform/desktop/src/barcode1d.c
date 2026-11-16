// barcode1d.c (platform/desktop/src): EAN-13 (UPC-A among them), EAN-8,
// UPC-E, Code 128 and Code 39, read along scan lines across the image in
// several directions, each line both ways: its gray levels become the
// widths of its dark and light runs, between edges where the level
// swings, and readers match those against each symbology's patterns, as
// ZXing's OneDReader does. A code counts once two lines agree on it.
// (Ported from the Go host's internal/barcode.)
#include "barcode_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SCAN_DIRECTIONS 12   // 15° apart: lines within 7.5° of across the bars
#define SCAN_SPACING 6.0     // pixels between parallel lines
#define MIN_AGREE 2          // lines that must read a code alike
#define MIN_SWING 14.0f      // gray levels: the least swing that makes an edge
#define SWING_SHARE 0.18f    // and the least share of the contrast around it
#define QUIET_MODULES 5.0f   // the least quiet zone, in modules, each side

typedef struct {
	float *v;
	size_t n, cap;
} floats;

static void fpush(floats *f, float v) {
	if (f->n == f->cap) {
		f->cap = f->cap ? f->cap * 2 : 256;
		f->v = realloc(f->v, f->cap * sizeof *f->v);
	}
	f->v[f->n++] = v;
}

static void freserve(floats *f, size_t n) {
	if (f->cap < n) {
		f->cap = n;
		f->v = realloc(f->v, n * sizeof *f->v);
	}
}

typedef struct {
	int pos;
	float v;
} extreme;

// A code read on one line: its kind, text and ends.
typedef struct {
	unsigned kind;
	char *text;
	double ax, ay, bx, by;
} read1d;

typedef struct {
	const uint8_t *pix;
	int w, h, stride;
	unsigned want;
	floats sig, thr, blo, bhi, runs, pos, rruns, rpos;
	extreme *ext;
	size_t next, ecap;
	read1d *reads;
	size_t nreads, rcap;
	// The line being read: its first point and direction.
	double x0, y0, dx, dy;
} scanner;

static float sum32(const float *r, int n) {
	float t = 0;
	for (int i = 0; i < n; i++) t += r[i];
	return t;
}

// match_variance is how far counters are from pattern, scaled to the
// same width: the mean error per unit, or +Inf if one element is off by
// more than max_individual units.
static float match_variance(const float *counters, const uint8_t *pattern, int n, float max_individual) {
	float total = 0;
	int plen = 0;
	for (int i = 0; i < n; i++) total += counters[i], plen += pattern[i];
	if (total < (float)plen * 0.8f) return INFINITY;
	float unit = total / (float)plen;
	max_individual *= unit;
	float v = 0;
	for (int i = 0; i < n; i++) {
		float d = fabsf(counters[i] - (float)pattern[i] * unit);
		if (d > max_individual) return INFINITY;
		v += d;
	}
	return v / total;
}

// ---- EAN and UPC ----
//
// Digits of seven modules in two bars and two spaces, between guards;
// left-hand digits in odd (L) or even (G) parity, whose pattern says
// EAN-13's first digit (UPC-E's number system and check digit).

#define UPC_MAX_AVG 0.48f
#define UPC_MAX_INDIVIDUAL 0.7f

// upc_l is each digit's L widths, space first; G is them reversed, and
// R is L's widths, bar first.
static const uint8_t upc_l[10][4] = {
    {3, 2, 1, 1}, {2, 2, 2, 1}, {2, 1, 2, 2}, {1, 4, 1, 1}, {1, 1, 3, 2}, {1, 2, 3, 1}, {1, 1, 1, 4}, {1, 3, 1, 2}, {1, 2, 1, 3}, {3, 1, 1, 2},
};

// ean_first_digit is, for EAN-13's first digit, which of the six left
// digits are G (bit 5 the first).
static const uint8_t ean_first_digit[10] = {0x00, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A};

// upce_parity is UPC-E's parity patterns by number system and check
// digit.
static const uint8_t upce_parity[2][10] = {
    {0x38, 0x34, 0x32, 0x31, 0x2C, 0x26, 0x23, 0x2A, 0x29, 0x25},
    {0x07, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A},
};

static const uint8_t guard3[] = {1, 1, 1}, guard5[] = {1, 1, 1, 1, 1}, guard6[] = {1, 1, 1, 1, 1, 1};

// upc_digit matches four runs to a digit: 0–9 for L (or R), 10–19 for G.
static int upc_digit(const float *r, bool g) {
	int best = -1;
	float best_v = UPC_MAX_AVG;
	for (int d = 0; d < 10; d++) {
		float v = match_variance(r, upc_l[d], 4, UPC_MAX_INDIVIDUAL);
		if (v < best_v) best = d, best_v = v;
		if (g) {
			uint8_t rev[4] = {upc_l[d][3], upc_l[d][2], upc_l[d][1], upc_l[d][0]};
			v = match_variance(r, rev, 4, UPC_MAX_INDIVIDUAL);
			if (v < best_v) best = d + 10, best_v = v;
		}
	}
	return best;
}

static bool guard_ok(const float *r, const uint8_t *p, int n) { return match_variance(r, p, n, UPC_MAX_INDIVIDUAL) < UPC_MAX_AVG; }

// ean_check reports whether digits' last is their check digit.
static bool ean_check(const char *digits, int n) {
	int sum = 0;
	for (int i = n - 2; i >= 0; i--) {
		int d = digits[i] - '0';
		if ((n - 2 - i) % 2 == 0) d *= 3;
		sum += d;
	}
	return (10 - sum % 10) % 10 == digits[n - 1] - '0';
}

// even_modules reports whether a code's guards and digits (left digits,
// then right ones after a middle guard, if any) are all about as wide
// as their modules say: one module size throughout, give or take what
// perspective does.
static bool even_modules(const float *runs, int n, int left, int right) {
	int total = 3 + 7 * left + 7 * right + 3 + (right > 0 ? 5 : 3);  // UPC-E's end guard is six modules
	float u = sum32(runs, n) / (float)total;
	int k = 0;
#define GROUP(cnt, modules)                                                     \
	do {                                                                        \
		float w_ = sum32(runs + k, cnt) / (float)(modules);                     \
		k += cnt;                                                               \
		if (!(w_ > 0.7f * u && w_ < 1.4f * u)) return false;                    \
	} while (0)
	GROUP(3, 3);
	for (int d = 0; d < left; d++) GROUP(4, 7);
	if (right == 0) {
		GROUP(6, 6);
		return true;
	}
	GROUP(5, 5);
	for (int d = 0; d < right; d++) GROUP(4, 7);
	GROUP(3, 3);
#undef GROUP
	return true;
}

static bool quiet_after(const float *runs, int len, int k, float m) { return k < len && runs[k] >= QUIET_MODULES * m; }

static bool decode_ean13(const float *runs, int len, int k, float m, char *out, int *end) {
	if (k + 56 > len) return false;
	char digits[14] = {0};
	int parity = 0;
	for (int d = 0; d < 6; d++) {
		int v = upc_digit(runs + k, true);
		if (v < 0) return false;
		if (v >= 10) parity |= 1 << (5 - d), v -= 10;
		digits[1 + d] = (char)('0' + v);
		k += 4;
	}
	int first = -1;
	for (int d = 0; d < 10; d++)
		if (ean_first_digit[d] == parity) first = d;
	if (first < 0 || !guard_ok(runs + k, guard5, 5)) return false;
	digits[0] = (char)('0' + first);
	k += 5;
	for (int d = 0; d < 6; d++) {
		int v = upc_digit(runs + k, false);
		if (v < 0) return false;
		digits[7 + d] = (char)('0' + v);
		k += 4;
	}
	if (!guard_ok(runs + k, guard3, 3) || !quiet_after(runs, len, k + 3, m) || !ean_check(digits, 13)) return false;
	memcpy(out, digits, 14);
	*end = k + 3;
	return true;
}

static bool decode_ean8(const float *runs, int len, int k, float m, char *out, int *end) {
	if (k + 40 > len) return false;
	char digits[9] = {0};
	for (int d = 0; d < 8; d++) {
		if (d == 4) {
			if (!guard_ok(runs + k, guard5, 5)) return false;
			k += 5;
		}
		int v = upc_digit(runs + k, false);
		if (v < 0) return false;
		digits[d] = (char)('0' + v);
		k += 4;
	}
	if (!guard_ok(runs + k, guard3, 3) || !quiet_after(runs, len, k + 3, m) || !ean_check(digits, 8)) return false;
	memcpy(out, digits, 9);
	*end = k + 3;
	return true;
}

// upce_to_upca expands a UPC-E's eight digits to the UPC-A's twelve.
static void upce_to_upca(const char *e, char *a) {
	const char *d = e + 1;
	char mid[11] = {0};
	char last = d[5];
	switch (last) {
	case '0':
	case '1':
	case '2':
		memcpy(mid, d, 2), mid[2] = last, memcpy(mid + 3, "0000", 4), memcpy(mid + 7, d + 2, 3);
		break;
	case '3':
		memcpy(mid, d, 3), memcpy(mid + 3, "00000", 5), memcpy(mid + 8, d + 3, 2);
		break;
	case '4':
		memcpy(mid, d, 4), memcpy(mid + 4, "00000", 5), mid[9] = d[4];
		break;
	default:
		memcpy(mid, d, 5), memcpy(mid + 5, "0000", 4), mid[9] = last;
	}
	a[0] = e[0];
	memcpy(a + 1, mid, 10);
	a[11] = e[7];
	a[12] = 0;
}

static bool decode_upce(const float *runs, int len, int k, float m, char *out, int *end) {
	if (k + 30 > len) return false;
	char digits[9] = {0};
	int parity = 0;
	for (int d = 0; d < 6; d++) {
		int v = upc_digit(runs + k, true);
		if (v < 0) return false;
		if (v >= 10) parity |= 1 << (5 - d), v -= 10;
		digits[1 + d] = (char)('0' + v);
		k += 4;
	}
	if (!guard_ok(runs + k, guard6, 6) || !quiet_after(runs, len, k + 6, m)) return false;
	bool ok = false;
	for (int sys = 0; sys < 2 && !ok; sys++)
		for (int c = 0; c < 10; c++)
			if (upce_parity[sys][c] == parity) {
				digits[0] = (char)('0' + sys), digits[7] = (char)('0' + c);
				ok = true;
				break;
			}
	char a[13];
	if (!ok) return false;
	upce_to_upca(digits, a);
	if (!ean_check(a, 12)) return false;
	memcpy(out, digits, 9);
	*end = k + 6;
	return true;
}

// decode_upcean reads an EAN-13 (UPC-A among them, as EAN-13), EAN-8 or
// UPC-E whose start guard is at runs[i]: its kind, text and the index of
// the quiet zone after it.
static unsigned decode_upcean(const float *runs, int len, int i, unsigned want, char *text, int *end) {
	if (i + 3 > len || !guard_ok(runs + i, guard3, 3)) return 0;
	float m = sum32(runs + i, 3) / 3;
	if (runs[i - 1] < QUIET_MODULES * m) return 0;
	if (want & (ASUKA_BARCODE_EAN13 | ASUKA_BARCODE_UPCA))
		if (decode_ean13(runs, len, i + 3, m, text, end) && even_modules(runs + i, *end - i, 6, 6)) return ASUKA_BARCODE_EAN13;
	if (want & ASUKA_BARCODE_EAN8)
		if (decode_ean8(runs, len, i + 3, m, text, end) && even_modules(runs + i, *end - i, 4, 4)) return ASUKA_BARCODE_EAN8;
	if (want & ASUKA_BARCODE_UPCE)
		if (decode_upce(runs, len, i + 3, m, text, end) && even_modules(runs + i, *end - i, 6, 0)) return ASUKA_BARCODE_UPCE;
	return 0;
}

// ---- Code 128 ----
//
// Symbols of three bars and three spaces, eleven modules; the stop
// symbol has a last bar of two more. Code sets A, B and C, a weighted
// check symbol modulo 103.

#define C128_MAX_AVG 0.25f
#define C128_MAX_INDIVIDUAL 0.7f
enum { C128_FNC1 = 102, C128_START_A = 103, C128_START_B = 104, C128_START_C = 105, C128_STOP = 106 };

static const uint8_t c128_patterns[107][6] = {
    {2, 1, 2, 2, 2, 2}, {2, 2, 2, 1, 2, 2}, {2, 2, 2, 2, 2, 1}, {1, 2, 1, 2, 2, 3}, {1, 2, 1, 3, 2, 2}, {1, 3, 1, 2, 2, 2}, {1, 2, 2, 2, 1, 3},
    {1, 2, 2, 3, 1, 2}, {1, 3, 2, 2, 1, 2}, {2, 2, 1, 2, 1, 3}, {2, 2, 1, 3, 1, 2}, {2, 3, 1, 2, 1, 2}, {1, 1, 2, 2, 3, 2}, {1, 2, 2, 1, 3, 2},
    {1, 2, 2, 2, 3, 1}, {1, 1, 3, 2, 2, 2}, {1, 2, 3, 1, 2, 2}, {1, 2, 3, 2, 2, 1}, {2, 2, 3, 2, 1, 1}, {2, 2, 1, 1, 3, 2}, {2, 2, 1, 2, 3, 1},
    {2, 1, 3, 2, 1, 2}, {2, 2, 3, 1, 1, 2}, {3, 1, 2, 1, 3, 1}, {3, 1, 1, 2, 2, 2}, {3, 2, 1, 1, 2, 2}, {3, 2, 1, 2, 2, 1}, {3, 1, 2, 2, 1, 2},
    {3, 2, 2, 1, 1, 2}, {3, 2, 2, 2, 1, 1}, {2, 1, 2, 1, 2, 3}, {2, 1, 2, 3, 2, 1}, {2, 3, 2, 1, 2, 1}, {1, 1, 1, 3, 2, 3}, {1, 3, 1, 1, 2, 3},
    {1, 3, 1, 3, 2, 1}, {1, 1, 2, 3, 1, 3}, {1, 3, 2, 1, 1, 3}, {1, 3, 2, 3, 1, 1}, {2, 1, 1, 3, 1, 3}, {2, 3, 1, 1, 1, 3}, {2, 3, 1, 3, 1, 1},
    {1, 1, 2, 1, 3, 3}, {1, 1, 2, 3, 3, 1}, {1, 3, 2, 1, 3, 1}, {1, 1, 3, 1, 2, 3}, {1, 1, 3, 3, 2, 1}, {1, 3, 3, 1, 2, 1}, {3, 1, 3, 1, 2, 1},
    {2, 1, 1, 3, 3, 1}, {2, 3, 1, 1, 3, 1}, {2, 1, 3, 1, 1, 3}, {2, 1, 3, 3, 1, 1}, {2, 1, 3, 1, 3, 1}, {3, 1, 1, 1, 2, 3}, {3, 1, 1, 3, 2, 1},
    {3, 3, 1, 1, 2, 1}, {3, 1, 2, 1, 1, 3}, {3, 1, 2, 3, 1, 1}, {3, 3, 2, 1, 1, 1}, {3, 1, 4, 1, 1, 1}, {2, 2, 1, 4, 1, 1}, {4, 3, 1, 1, 1, 1},
    {1, 1, 1, 2, 2, 4}, {1, 1, 1, 4, 2, 2}, {1, 2, 1, 1, 2, 4}, {1, 2, 1, 4, 2, 1}, {1, 4, 1, 1, 2, 2}, {1, 4, 1, 2, 2, 1}, {1, 1, 2, 2, 1, 4},
    {1, 1, 2, 4, 1, 2}, {1, 2, 2, 1, 1, 4}, {1, 2, 2, 4, 1, 1}, {1, 4, 2, 1, 1, 2}, {1, 4, 2, 2, 1, 1}, {2, 4, 1, 2, 1, 1}, {2, 2, 1, 1, 1, 4},
    {4, 1, 3, 1, 1, 1}, {2, 4, 1, 1, 1, 2}, {1, 3, 4, 1, 1, 1}, {1, 1, 1, 2, 4, 2}, {1, 2, 1, 1, 4, 2}, {1, 2, 1, 2, 4, 1}, {1, 1, 4, 2, 1, 2},
    {1, 2, 4, 1, 1, 2}, {1, 2, 4, 2, 1, 1}, {4, 1, 1, 2, 1, 2}, {4, 2, 1, 1, 1, 2}, {4, 2, 1, 2, 1, 1}, {2, 1, 2, 1, 4, 1}, {2, 1, 4, 1, 2, 1},
    {4, 1, 2, 1, 2, 1}, {1, 1, 1, 1, 4, 3}, {1, 1, 1, 3, 4, 1}, {1, 3, 1, 1, 4, 1}, {1, 1, 4, 1, 1, 3}, {1, 1, 4, 3, 1, 1}, {4, 1, 1, 1, 1, 3},
    {4, 1, 1, 3, 1, 1}, {1, 1, 3, 1, 4, 1}, {1, 1, 4, 1, 3, 1}, {3, 1, 1, 1, 4, 1}, {4, 1, 1, 1, 3, 1}, {2, 1, 1, 4, 1, 2}, {2, 1, 1, 2, 1, 4},
    {2, 1, 1, 2, 3, 2}, {2, 3, 3, 1, 1, 1},
};

// c128_symbol matches six runs to a symbol, or -1.
static int c128_symbol(const float *r) {
	int best = -1;
	float best_v = C128_MAX_AVG;
	for (int c = 0; c < 107; c++) {
		float v = match_variance(r, c128_patterns[c], 6, C128_MAX_INDIVIDUAL);
		if (v < best_v) best = c, best_v = v;
	}
	return best;
}

typedef struct {
	char *p;
	size_t n, cap;
} text_buf;

static void tput(text_buf *t, const char *s, size_t n) {
	if (t->n + n + 1 > t->cap) {
		t->cap = (t->n + n + 1) * 2;
		t->p = realloc(t->p, t->cap);
	}
	memcpy(t->p + t->n, s, n);
	t->n += n;
	t->p[t->n] = 0;
}

// tput_char writes a character of Latin-1 (0 to 255) as UTF-8.
static void tput_char(text_buf *t, int c) {
	char b[2];
	if (c < 0x80) {
		b[0] = (char)c;
		tput(t, b, 1);
	} else {
		b[0] = (char)(0xC0 | c >> 6), b[1] = (char)(0x80 | (c & 0x3F));
		tput(t, b, 2);
	}
}

// fnc4 is FNC4: once, the next character is extended (+128); twice,
// every one is until it's done twice again.
static void fnc4(bool *upper, bool *shift_upper) {
	if (!*upper && *shift_upper) *upper = true, *shift_upper = false;
	else if (*upper && *shift_upper) *upper = false, *shift_upper = false;
	else *shift_upper = true;
}

// c128_text interprets the symbols after the start one (malloc's), NULL
// if they don't make text.
static char *c128_text(int start, const int *codes, int n) {
	enum { SET_A, SET_B, SET_C };
	int set = start - C128_START_A;
	text_buf out = {0};
	bool upper = false, shift_upper = false;
#define PUT(ch)                                    \
	do {                                           \
		int c_ = (ch);                             \
		if (upper != shift_upper) c_ += 128;       \
		shift_upper = false;                       \
		tput_char(&out, c_);                       \
	} while (0)
	for (int k = 0; k < n; k++) {
		int c = codes[k];
		int cur = set;
		for (bool again = true; again;) {
			again = false;
			if (cur == SET_A || cur == SET_B) {
				if (cur == SET_A && c < 64) PUT(' ' + c);
				else if (cur == SET_A && c < 96) PUT(c - 64);
				else if (cur == SET_B && c < 96) PUT(' ' + c);
				else
					switch (c) {
					case C128_FNC1:
						if (k > 0) tput(&out, "\x1d", 1);
						break;
					case 96:  // FNC3
					case 97:  // FNC2
						break;
					case 98:  // shift: the next symbol in the other set
						if (k + 1 >= n) {
							free(out.p);
							return NULL;
						}
						c = codes[++k];
						cur = cur == SET_A ? SET_B : SET_A;
						again = true;
						break;
					case 99:
						set = SET_C;
						break;
					case 100:
						if (cur == SET_A) set = SET_B;
						else fnc4(&upper, &shift_upper);
						break;
					case 101:
						if (cur == SET_B) set = SET_A;
						else fnc4(&upper, &shift_upper);
						break;
					}
			} else if (c < 100) {
				char d[2] = {(char)('0' + c / 10), (char)('0' + c % 10)};
				tput(&out, d, 2);
			} else {
				switch (c) {
				case C128_FNC1:
					if (k > 0) tput(&out, "\x1d", 1);
					break;
				case 100:
					set = SET_B;
					break;
				case 101:
					set = SET_A;
					break;
				}
			}
		}
	}
#undef PUT
	if (!out.p) out.p = calloc(1, 1);
	return out.p;
}

// decode_code128 reads a Code 128 whose start symbol is at runs[i]: its
// text (malloc's) and the index of the quiet zone after it.
static char *decode_code128(const float *runs, int len, int i, int *end) {
	if (i + 6 > len) return NULL;
	int start = -1;
	float best_v = C128_MAX_AVG;
	for (int c = C128_START_A; c <= C128_START_C; c++) {
		float v = match_variance(runs + i, c128_patterns[c], 6, C128_MAX_INDIVIDUAL);
		if (v < best_v) start = c, best_v = v;
	}
	if (start < 0) return NULL;
	float width = sum32(runs + i, 6);
	if (runs[i - 1] < width / 2) return NULL;
	int codes[256], n = 0;
	int k = i + 6;
	for (;;) {
		if (k + 6 > len || n == (int)(sizeof codes / sizeof *codes)) return NULL;
		int c = c128_symbol(runs + k);
		if (c < 0 || (c >= C128_START_A && c <= C128_START_C)) return NULL;
		float w = sum32(runs + k, 6);
		if (w < 0.7f * width || w > 1.4f * width) return NULL;  // symbols keep their width
		if (c == C128_STOP) {
			float m = w / 11;
			if (k + 7 >= len) return NULL;
			float last = runs[k + 6];
			if (last < 1.3f * m || last > 2.7f * m || runs[k + 7] < w / 2) return NULL;
			k += 7;
			break;
		}
		codes[n++] = c;
		k += 6;
	}
	if (n < 2) return NULL;
	int sum = start;
	for (int j = 0; j < n - 1; j++) sum += (j + 1) * codes[j];
	if (sum % 103 != codes[n - 1]) return NULL;
	char *text = c128_text(start, codes, n - 1);
	if (!text || !*text) {
		free(text);
		return NULL;
	}
	*end = k;
	return text;
}

// ---- Code 39 ----
//
// Characters of five bars and four spaces, three of them wide, apart by
// a narrow space, between * start and stop characters. No check
// character is read.

static const char code39_alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%";
static const uint16_t code39_patterns[43] = {
    0x034, 0x121, 0x061, 0x160, 0x031, 0x130, 0x070, 0x025, 0x124, 0x064, 0x109, 0x049, 0x148, 0x019, 0x118,
    0x058, 0x00D, 0x10C, 0x04C, 0x01C, 0x103, 0x043, 0x142, 0x013, 0x112, 0x052, 0x007, 0x106, 0x046, 0x016,
    0x181, 0x0C1, 0x1C0, 0x091, 0x190, 0x0D0, 0x085, 0x184, 0x0C4, 0x0A8, 0x0A2, 0x08A, 0x02A,
};
#define CODE39_ASTERISK 0x094

// code39_wide sorts nine runs into narrow and wide: the pattern of the
// three wide ones, or -1 if they don't split cleanly.
static int code39_wide(const float *r) {
	int idx[9];
	for (int i = 0; i < 9; i++) idx[i] = i;
	for (int a = 0; a < 9; a++)
		for (int b = a + 1; b < 9; b++)
			if (r[idx[b]] > r[idx[a]]) {
				int t = idx[a];
				idx[a] = idx[b], idx[b] = t;
			}
	float narrow_max = r[idx[3]], wide_min = r[idx[2]];
	float nsum = 0, wsum = 0;
	for (int k = 3; k < 9; k++) nsum += r[idx[k]];
	for (int k = 0; k < 3; k++) wsum += r[idx[k]];
	float n = nsum / 6, w = wsum / 3;
	if (w < 1.45f * n || w > 4 * n || wide_min < 1.15f * narrow_max || narrow_max > 1.6f * n || r[idx[0]] > 1.6f * w) return -1;
	int p = 0;
	for (int k = 0; k < 3; k++) p |= 1 << (8 - idx[k]);
	return p;
}

static char code39_char(int p) {
	for (int i = 0; i < 43; i++)
		if (code39_patterns[i] == p) return code39_alphabet[i];
	return 0;
}

// decode_code39 reads a Code 39 whose start character is at runs[i]:
// its text (malloc's) and the index of the quiet zone after it.
static char *decode_code39(const float *runs, int len, int i, int *end) {
	// The start character: narrow, wide, narrow, narrow, wide (bars and
	// spaces from the first).
	if (i + 10 > len || runs[i + 1] < 1.3f * runs[i] || runs[i + 4] < 1.3f * runs[i + 3] || code39_wide(runs + i) != CODE39_ASTERISK) return NULL;
	float width = sum32(runs + i, 9);
	if (runs[i - 1] < width / 2) return NULL;
	float narrow = width / 15;
	text_buf text = {0};
	int k = i + 9;
	for (;;) {
		// The gap between characters is narrow.
		if (k + 10 > len || runs[k] > 3 * narrow) break;
		k++;
		float w = sum32(runs + k, 9);
		if (w < 0.75f * width || w > 1.33f * width) break;
		int p = code39_wide(runs + k);
		if (p < 0) break;
		if (p == CODE39_ASTERISK) {
			if (k + 9 >= len || runs[k + 9] < w / 2 || text.n == 0) break;
			*end = k + 9;
			return text.p;
		}
		char c = code39_char(p);
		if (!c) break;
		tput(&text, &c, 1);
		width = (width + w) / 2;
		k += 9;
	}
	free(text.p);
	return NULL;
}

// ---- Lines ----

static void add_read(scanner *s, unsigned kind, char *text, const float *pos, int i, int j) {
	if (s->nreads == s->rcap) {
		s->rcap = s->rcap ? s->rcap * 2 : 16;
		s->reads = realloc(s->reads, s->rcap * sizeof *s->reads);
	}
	read1d *r = &s->reads[s->nreads++];
	r->kind = kind;
	r->text = text;
	r->ax = s->x0 + pos[i] * s->dx, r->ay = s->y0 + pos[i] * s->dy;
	r->bx = s->x0 + pos[j] * s->dx, r->by = s->y0 + pos[j] * s->dy;
}

// decode_runs tries every reader wanted at every dark run after a light
// one.
static void decode_runs(scanner *s, const float *runs, const float *pos, int len, bool first_dark) {
	for (int i = first_dark ? 2 : 1; i + 8 < len; i += 2) {
		if (runs[i - 1] < 2 * runs[i]) continue;
		int j;
		if (s->want & (ASUKA_BARCODE_EAN13 | ASUKA_BARCODE_UPCA | ASUKA_BARCODE_EAN8 | ASUKA_BARCODE_UPCE)) {
			char digits[14];
			unsigned kind = decode_upcean(runs, len, i, s->want, digits, &j);
			if (kind) {
				add_read(s, kind, asuka_barcode_strdup(digits), pos, i, j);
				i = j - 1;
				continue;
			}
		}
		char *text;
		if ((s->want & ASUKA_BARCODE_CODE128) && (text = decode_code128(runs, len, i, &j))) {
			add_read(s, ASUKA_BARCODE_CODE128, text, pos, i, j);
			i = j - 1;
			continue;
		}
		if ((s->want & ASUKA_BARCODE_CODE39) && (text = decode_code39(runs, len, i, &j))) {
			add_read(s, ASUKA_BARCODE_CODE39, text, pos, i, j);
			i = j - 1;
			continue;
		}
	}
}

static void push_extreme(scanner *s, int pos, float v) {
	if (s->next == s->ecap) {
		s->ecap = s->ecap ? s->ecap * 2 : 256;
		s->ext = realloc(s->ext, s->ecap * sizeof *s->ext);
	}
	s->ext[s->next++] = (extreme){pos, v};
}

// edges splits sig into runs at the edges between its swings, and
// reports whether the first run is dark. A swing counts when it's a
// good part of the contrast around it, so noise in flat parts and
// uneven light don't make edges.
static bool edges(scanner *s, const float *sig, int n) {
	const int blk = 16;
	int nb = (n + blk - 1) / blk;
	freserve(&s->thr, (size_t)nb);
	freserve(&s->blo, (size_t)nb);
	freserve(&s->bhi, (size_t)nb);
	float *thr = s->thr.v, *blo = s->blo.v, *bhi = s->bhi.v;
	// Each block's range, then the range of the five blocks around it.
	for (int b = 0; b < nb; b++) {
		int e = (b + 1) * blk < n ? (b + 1) * blk : n;
		float mn = sig[b * blk], mx = mn;
		for (int k = b * blk; k < e; k++) mn = fminf(mn, sig[k]), mx = fmaxf(mx, sig[k]);
		blo[b] = mn, bhi[b] = mx;
	}
	for (int b = 0; b < nb; b++) {
		float mn = blo[b], mx = bhi[b];
		for (int j = b - 2 > 0 ? b - 2 : 0; j <= (b + 2 < nb - 1 ? b + 2 : nb - 1); j++) mn = fminf(mn, blo[j]), mx = fmaxf(mx, bhi[j]);
		thr[b] = fmaxf(MIN_SWING, SWING_SHARE * (mx - mn));
	}
	// Alternate extremes, each swing at least the threshold.
	s->next = 0;
	float hi_v = sig[0], lo_v = sig[0];
	int hi_p = 0, lo_p = 0, dir = 0;
	for (int k = 1; k < n; k++) {
		float v = sig[k], t = thr[k / blk];
		switch (dir) {
		case 0:
			if (v > hi_v) hi_v = v, hi_p = k;
			if (v < lo_v) lo_v = v, lo_p = k;
			if (hi_v - lo_v >= t) {
				if (hi_p > lo_p) {
					push_extreme(s, lo_p, lo_v);
					dir = 1;
				} else {
					push_extreme(s, hi_p, hi_v);
					dir = -1;
					lo_v = v, lo_p = k;
				}
			}
			break;
		case 1:
			if (v > hi_v) hi_v = v, hi_p = k;
			else if (hi_v - v >= t) {
				push_extreme(s, hi_p, hi_v);
				dir = -1;
				lo_v = v, lo_p = k;
			}
			break;
		default:
			if (v < lo_v) lo_v = v, lo_p = k;
			else if (v - lo_v >= t) {
				push_extreme(s, lo_p, lo_v);
				dir = 1;
				hi_v = v, hi_p = k;
			}
		}
	}
	// The last extreme, if the level got there.
	if (s->next > 0) {
		extreme last = s->ext[s->next - 1];
		float t = thr[(n - 1) / blk];
		if (dir == 1 && hi_v - last.v >= t) push_extreme(s, hi_p, hi_v);
		else if (dir == -1 && last.v - lo_v >= t) push_extreme(s, lo_p, lo_v);
	}
	s->runs.n = s->pos.n = 0;
	if (s->next < 2) return false;
	// An edge where the level crosses the middle of two extremes.
	float prev = 0;
	fpush(&s->pos, 0);
	for (size_t i = 0; i + 1 < s->next; i++) {
		extreme a = s->ext[i], b = s->ext[i + 1];
		float mid = (a.v + b.v) / 2, e = (float)a.pos;
		for (int k = a.pos; k < b.pos; k++) {
			float v0 = sig[k] - mid, v1 = sig[k + 1] - mid;
			if ((v0 <= 0) != (v1 <= 0)) {
				e = (float)k + v0 / (v0 - v1);
				break;
			}
		}
		fpush(&s->runs, e - prev);
		fpush(&s->pos, e);
		prev = e;
	}
	fpush(&s->runs, (float)(n - 1) - prev);
	// The first run is dark if its extreme is a trough.
	return s->ext[0].v < s->ext[1].v;
}

// scan_line reads the line through (px, py) in direction (dx, dy).
static void scan_line(scanner *s, double px, double py, double dx, double dy) {
	double w = s->w - 1, h = s->h - 1;
	double t0 = -INFINITY, t1 = INFINITY;
	double ps[2] = {px, py}, ds[2] = {dx, dy}, maxs[2] = {w, h};
	for (int k = 0; k < 2; k++) {
		if (ds[k] == 0) {
			if (ps[k] < 0 || ps[k] > maxs[k]) return;
			continue;
		}
		double a = -ps[k] / ds[k], b = (maxs[k] - ps[k]) / ds[k];
		if (a > b) {
			double t = a;
			a = b, b = t;
		}
		t0 = fmax(t0, a), t1 = fmin(t1, b);
	}
	t0 = ceil(t0 - 1e-9);
	if (!(t1 - t0 >= 39)) return;  // fewer than 40 points
	int n = (int)(t1 - t0) + 1;
	freserve(&s->sig, (size_t)n);
	float *sig = s->sig.v;
	double x0 = px + t0 * dx, y0 = py + t0 * dy;
	const uint8_t *pix = s->pix;
	int st = s->stride;
	if (dy == 0) {
		const uint8_t *row = pix + (size_t)lround(y0) * st;
		long xs = lround(x0);
		for (int i = 0; i < n; i++) sig[i] = row[xs + i];
	} else if (dx == 0) {
		long xs = lround(x0), ys = lround(y0);
		for (int i = 0; i < n; i++) sig[i] = pix[(size_t)(ys + i) * st + xs];
	} else {
		float fdx = (float)dx, fdy = (float)dy, fx = (float)x0, fy = (float)y0;
		for (int i = 0; i < n; i++) {
			float x = fx + (float)i * fdx, y = fy + (float)i * fdy;
			int ix = (int)x, iy = (int)y;
			if (ix >= s->w - 1) ix = s->w - 2;
			if (iy >= s->h - 1) iy = s->h - 2;
			if (ix < 0) ix = 0;
			if (iy < 0) iy = 0;
			float ax = x - (float)ix, ay = y - (float)iy;
			const uint8_t *p = pix + (size_t)iy * st + ix;
			float top = p[0] + ax * ((float)p[1] - p[0]);
			float bot = p[st] + ax * ((float)p[st + 1] - p[st]);
			sig[i] = top + ay * (bot - top);
		}
	}
	bool first_dark = edges(s, sig, n);
	int m = (int)s->runs.n;
	if (m < 20) return;
	s->x0 = x0, s->y0 = y0, s->dx = dx, s->dy = dy;
	decode_runs(s, s->runs.v, s->pos.v, m, first_dark);
	// And the other way.
	s->rruns.n = s->rpos.n = 0;
	for (int k = m - 1; k >= 0; k--) {
		fpush(&s->rruns, s->runs.v[k]);
		fpush(&s->rpos, s->pos.v[k] + s->runs.v[k]);
	}
	bool last_dark = first_dark == (m % 2 == 1);
	decode_runs(s, s->rruns.v, s->rpos.v, m, last_dark);
}

typedef struct {
	unsigned kind;
	const char *text;
	double cx, cy, length;
	int n;
	double x0, y0, x1, y1;
	size_t order;
} group;

static int by_count(const void *a, const void *b) {
	const group *x = a, *y = b;
	if (x->n != y->n) return y->n - x->n;
	return x->order < y->order ? -1 : x->order > y->order;  // stable
}

// agree groups the reads by code and place: the codes read on enough
// lines.
static size_t agree(scanner *s, bc_found **out) {
	group *gs = calloc(s->nreads ? s->nreads : 1, sizeof *gs);
	size_t ng = 0;
	for (size_t ri = 0; ri < s->nreads; ri++) {
		read1d *r = &s->reads[ri];
		double mx = (r->ax + r->bx) / 2, my = (r->ay + r->by) / 2, l = hypot(r->ax - r->bx, r->ay - r->by);
		group *g = NULL;
		for (size_t i = 0; i < ng && !g; i++)
			if (gs[i].kind == r->kind && strcmp(gs[i].text, r->text) == 0 && hypot(gs[i].cx - mx, gs[i].cy - my) < 0.6 * fmax(l, gs[i].length)) g = &gs[i];
		if (!g) {
			g = &gs[ng];
			*g = (group){.kind = r->kind, .text = r->text, .x0 = INFINITY, .y0 = INFINITY, .x1 = -INFINITY, .y1 = -INFINITY, .order = ng};
			ng++;
		}
		double f = g->n;
		g->cx = (g->cx * f + mx) / (f + 1);
		g->cy = (g->cy * f + my) / (f + 1);
		g->length = (g->length * f + l) / (f + 1);
		g->n++;
		g->x0 = fmin(g->x0, fmin(r->ax, r->bx)), g->y0 = fmin(g->y0, fmin(r->ay, r->by));
		g->x1 = fmax(g->x1, fmax(r->ax, r->bx)), g->y1 = fmax(g->y1, fmax(r->ay, r->by));
	}
	// One place, one code: where reads of different codes overlap, the
	// code most lines read wins.
	qsort(gs, ng, sizeof *gs, by_count);
	size_t *kept = calloc(ng ? ng : 1, sizeof *kept), nk = 0, n = 0;
	bc_found *found = calloc(ng ? ng : 1, sizeof *found);
	for (size_t gi = 0; gi < ng; gi++) {
		group *g = &gs[gi];
		if (g->n < MIN_AGREE) continue;
		bool clash = false;
		for (size_t k = 0; k < nk && !clash; k++) {
			group *o = &gs[kept[k]];
			clash = g->cx > o->x0 && g->cx < o->x1 && g->cy > o->y0 && g->cy < o->y1;
		}
		if (clash) continue;
		kept[nk++] = gi;
		unsigned kind = g->kind;
		const char *text = g->text;
		if (kind == ASUKA_BARCODE_EAN13) {
			// A UPC-A is an EAN-13 that starts with 0.
			if (text[0] == '0' && (s->want & ASUKA_BARCODE_UPCA)) kind = ASUKA_BARCODE_UPCA, text++;
			else if (!(s->want & ASUKA_BARCODE_EAN13)) continue;
		}
		found[n++] = (bc_found){kind, asuka_barcode_strdup(text), (int)floor(g->x0), (int)floor(g->y0), (int)ceil(g->x1) + 1, (int)ceil(g->y1) + 1};
	}
	free(kept);
	free(gs);
	*out = found;
	return n;
}

size_t bc_decode_1d(const uint8_t *gray, int w, int h, int stride, unsigned want, bc_found **out) {
	*out = NULL;
	if (w < 2 || h < 2) return 0;
	scanner s = {.pix = gray, .w = w, .h = h, .stride = stride, .want = want};
	for (int k = 0; k < SCAN_DIRECTIONS; k++) {
		double th = k * 3.14159265358979323846 / SCAN_DIRECTIONS, dx = cos(th), dy = sin(th);
		if (fabs(dx) < 1e-9) dx = 0;
		if (fabs(dy) < 1e-9) dy = 0;
		double nx = -dy, ny = dx, lo = INFINITY, hi = -INFINITY;
		double cs[4][2] = {{0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1}};
		for (int c = 0; c < 4; c++) {
			double o = cs[c][0] * nx + cs[c][1] * ny;
			lo = fmin(lo, o), hi = fmax(hi, o);
		}
		for (double o = lo + SCAN_SPACING / 2; o <= hi; o += SCAN_SPACING) scan_line(&s, o * nx, o * ny, dx, dy);
	}
	size_t n = agree(&s, out);
	for (size_t i = 0; i < s.nreads; i++) free(s.reads[i].text);
	free(s.reads);
	floats *fs[] = {&s.sig, &s.thr, &s.blo, &s.bhi, &s.runs, &s.pos, &s.rruns, &s.rpos};
	for (size_t i = 0; i < sizeof fs / sizeof *fs; i++) free(fs[i]->v);
	free(s.ext);
	return n;
}
