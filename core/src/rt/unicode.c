// unicode.c: UTF-8, grapheme clusters, terminal widths and case mapping
// over the generated tables (unicode_gen.c). See unicode.h.
#include "rt/unicode.h"

uint32_t ak_utf8_decode(const char *str, size_t n, size_t *i) {
	const uint8_t *p = (const uint8_t *)str + *i;
	size_t left = n - *i;
	uint8_t c = p[0];
	if (c < 0x80) {
		*i += 1;
		return c;
	}
	size_t len;
	uint32_t min;
	if ((c & 0xe0) == 0xc0) len = 2, min = 0x80;
	else if ((c & 0xf0) == 0xe0) len = 3, min = 0x800;
	else if ((c & 0xf8) == 0xf0) len = 4, min = 0x10000;
	else goto bad;
	if (left < len) goto bad;
	uint32_t cp = c & (0x7f >> len);
	for (size_t k = 1; k < len; k++) {
		if ((p[k] & 0xc0) != 0x80) goto bad;
		cp = cp << 6 | (p[k] & 0x3f);
	}
	if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) goto bad;
	*i += len;
	return cp;
bad:
	*i += 1;
	return 0xfffd;
}

size_t ak_utf8_encode(uint32_t c, char *out) {
	if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) c = 0xfffd;
	if (c < 0x80) {
		out[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		out[0] = (char)(0xc0 | c >> 6);
		out[1] = (char)(0x80 | (c & 0x3f));
		return 2;
	}
	if (c < 0x10000) {
		out[0] = (char)(0xe0 | c >> 12);
		out[1] = (char)(0x80 | (c >> 6 & 0x3f));
		out[2] = (char)(0x80 | (c & 0x3f));
		return 3;
	}
	out[0] = (char)(0xf0 | c >> 18);
	out[1] = (char)(0x80 | (c >> 12 & 0x3f));
	out[2] = (char)(0x80 | (c >> 6 & 0x3f));
	out[3] = (char)(0x80 | (c & 0x3f));
	return 4;
}

uint16_t ak_uprops(uint32_t cp) {
	if (cp > 0x10ffff) cp = 0xfffd;
	// The last range that starts at or before cp.
	uint32_t lo = 0, hi = ak_uprop_count;
	while (hi - lo > 1) {
		uint32_t mid = lo + (hi - lo) / 2;
		if (ak_uprop_starts[mid] <= cp) lo = mid;
		else hi = mid;
	}
	return ak_uprop_values[lo];
}

uint8_t ak_ucat(uint32_t cp) {
	if (cp > 0x10ffff) return AK_GC_CN;
	uint32_t lo = 0, hi = ak_ucat_count;
	while (hi - lo > 1) {
		uint32_t mid = lo + (hi - lo) / 2;
		if (ak_ucat_starts[mid] <= cp) lo = mid;
		else hi = mid;
	}
	return ak_ucat_values[lo];
}

// ---- Grapheme clusters (UAX #29) ----

size_t ak_grapheme_end(const char *s, size_t n, size_t i) {
	if (i >= n) return n;
	size_t j = i;
	uint16_t p = ak_uprops(ak_utf8_decode(s, n, &j));
	int prev = AK_UP_GCB(p);
	int ri = prev == AK_GCB_RI;             // regional indicators in a row, up to prev
	int emoji = p & AK_UP_EXTPICT ? 1 : 0;  // 1: ExtPict Extend*; 2: and then ZWJ
	bool linker = AK_UP_INCB(p) == AK_INCB_LINKER;  // InCB Linker, InCB Extend*
	while (j < n) {
		size_t k = j;
		uint16_t q = ak_uprops(ak_utf8_decode(s, n, &k));
		int g = AK_UP_GCB(q);
		bool join;
		if (prev == AK_GCB_CR && g == AK_GCB_LF) join = true;  // GB3
		else if (prev == AK_GCB_CR || prev == AK_GCB_LF || prev == AK_GCB_CONTROL) join = false;  // GB4
		else if (g == AK_GCB_CR || g == AK_GCB_LF || g == AK_GCB_CONTROL) join = false;  // GB5
		else if (prev == AK_GCB_L && (g == AK_GCB_L || g == AK_GCB_V || g == AK_GCB_LV || g == AK_GCB_LVT)) join = true;  // GB6
		else if ((prev == AK_GCB_LV || prev == AK_GCB_V) && (g == AK_GCB_V || g == AK_GCB_T)) join = true;  // GB7
		else if ((prev == AK_GCB_LVT || prev == AK_GCB_T) && g == AK_GCB_T) join = true;  // GB8
		else if (g == AK_GCB_EXTEND || g == AK_GCB_ZWJ || g == AK_GCB_SPACING_MARK) join = true;  // GB9, GB9a
		else if (prev == AK_GCB_PREPEND) join = true;  // GB9b
		else if (linker && AK_UP_INCB(q) == AK_INCB_CONSONANT) join = true;  // GB9c
		else if (emoji == 2 && (q & AK_UP_EXTPICT)) join = true;  // GB11
		else if (prev == AK_GCB_RI && g == AK_GCB_RI) join = ri % 2 == 1;  // GB12, GB13
		else join = false;  // GB999
		if (!join) break;
		ri = g == AK_GCB_RI ? ri + 1 : 0;
		if (q & AK_UP_EXTPICT) emoji = 1;
		else if (emoji == 1 && g == AK_GCB_ZWJ) emoji = 2;
		else if (!(emoji == 1 && g == AK_GCB_EXTEND)) emoji = 0;
		int incb = AK_UP_INCB(q);
		linker = incb == AK_INCB_LINKER || (linker && incb == AK_INCB_EXTEND);
		prev = g;
		j = k;
	}
	return j;
}

// ---- Terminal widths ----

// st ends a string a control sequence holds (an OSC's, a DCS's): BEL,
// ESC \, or C1's ST (U+009C); the bytes after it.
static size_t to_st(const char *s, size_t n, size_t j) {
	while (j < n) {
		unsigned char c = (unsigned char)s[j];
		if (c == 0x07) return j + 1;
		if (c == 0x1b && j + 1 < n && s[j + 1] == '\\') return j + 2;
		if (c == 0xc2 && j + 1 < n && (unsigned char)s[j + 1] == 0x9c) return j + 2;
		j++;
	}
	return n;
}

// csi reads a control sequence's parameters and intermediates to its
// final byte, from s[j].
static size_t csi(const char *s, size_t n, size_t j) {
	while (j < n && (unsigned char)s[j] >= 0x20 && (unsigned char)s[j] <= 0x3f) j++;
	if (j < n && (unsigned char)s[j] >= 0x40 && (unsigned char)s[j] <= 0x7e) return j + 1;
	return j;
}

size_t ak_escape_len(const char *s, size_t n, size_t i) {
	unsigned char c = (unsigned char)s[i];
	if (c == 0xc2 && i + 1 < n && (unsigned char)s[i + 1] == 0x9b) return csi(s, n, i + 2) - i;  // C1's CSI
	if (c != 0x1b) return 0;
	if (i + 1 >= n) return 1;
	unsigned char d = (unsigned char)s[i + 1];
	switch (d) {
	case '[': return csi(s, n, i + 2) - i;
	case ']': case 'P': case 'X': case '^': case '_': return to_st(s, n, i + 2) - i;  // OSC, DCS, SOS, PM, APC
	}
	if (d >= 0x20 && d <= 0x2f) {
		// nF: intermediates, then a final byte (ESC ( B).
		size_t j = i + 1;
		while (j < n && (unsigned char)s[j] >= 0x20 && (unsigned char)s[j] <= 0x2f) j++;
		return (j < n && (unsigned char)s[j] >= 0x30 && (unsigned char)s[j] <= 0x7e ? j + 1 : j) - i;
	}
	if (d >= 0x30 && d <= 0x7e) return 2;  // ESC 7, ESC c, ...
	return 1;
}

int ak_cluster_width(const char *s, size_t n) {
	size_t i = 0;
	uint16_t p0 = ak_uprops(ak_utf8_decode(s, n, &i));
	int sum = (int)AK_UP_WIDTH(p0);
	if (i >= n) return sum;
	bool vs16 = false, vs15 = false, zwj = false;
	while (i < n) {
		uint32_t c = ak_utf8_decode(s, n, &i);
		if (c == 0xfe0f) vs16 = true;
		else if (c == 0xfe0e) vs15 = true;
		else if (c == 0x200d) zwj = true;
		sum += (int)AK_UP_WIDTH(ak_uprops(c));
	}
	if (p0 & AK_UP_EVS) {
		if (vs16) return 2;
		if (vs15 && !zwj) return 1;
	}
	if (p0 & AK_UP_EXTPICT) {
		// An emoji modifier sequence or a ZWJ sequence is one emoji.
		if (zwj || AK_UP_WIDTH(p0) == 2) return 2;
		return (int)AK_UP_WIDTH(p0);
	}
	return sum;
}

int64_t ak_text_width(const char *s, size_t n) {
	int64_t w = 0;
	size_t i = 0;
	while (i < n) {
		unsigned char c = (unsigned char)s[i];
		if (c >= 0x20 && c < 0x7f && (i + 1 == n || (unsigned char)s[i + 1] < 0x80)) {
			// Printable ASCII that nothing joins.
			w++;
			i++;
			continue;
		}
		size_t e = ak_escape_len(s, n, i);
		if (e) {
			i += e;
			continue;
		}
		size_t j = ak_grapheme_end(s, n, i);
		w += ak_cluster_width(s + i, j - i);
		i = j;
	}
	return w;
}

// ---- Case mapping ----

int ak_case_map(uint32_t cp, ak_case_kind k, uint32_t out[3]) {
	static const uint16_t bits[] = {AK_UP_LOWER, AK_UP_UPPER, AK_UP_FOLD};
	out[0] = cp;
	if (cp < 0x80) {
		if (k == AK_CASE_UPPER ? cp >= 'a' && cp <= 'z' : cp >= 'A' && cp <= 'Z') out[0] = k == AK_CASE_UPPER ? cp - 32 : cp + 32;
		return 1;
	}
	if (!(ak_uprops(cp) & bits[k])) return 1;
	const ak_case_full *full = k == AK_CASE_LOWER ? ak_case_lower_full : k == AK_CASE_UPPER ? ak_case_upper_full : ak_case_fold_full;
	uint32_t nfull = k == AK_CASE_LOWER ? ak_case_lower_full_count : k == AK_CASE_UPPER ? ak_case_upper_full_count : ak_case_fold_full_count;
	uint32_t lo = 0, hi = nfull;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		if (full[mid].cp < cp) lo = mid + 1;
		else hi = mid;
	}
	if (lo < nfull && full[lo].cp == cp) {
		int m = 0;
		while (m < 3 && full[lo].to[m]) {
			out[m] = full[lo].to[m];
			m++;
		}
		return m;
	}
	const ak_case_run *runs = k == AK_CASE_LOWER ? ak_case_lower_runs : k == AK_CASE_UPPER ? ak_case_upper_runs : ak_case_fold_runs;
	uint32_t nruns = k == AK_CASE_LOWER ? ak_case_lower_runs_count : k == AK_CASE_UPPER ? ak_case_upper_runs_count : ak_case_fold_runs_count;
	// The last run that starts at or before cp.
	lo = 0, hi = nruns;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		if (runs[mid].start <= cp) lo = mid + 1;
		else hi = mid;
	}
	if (lo == 0) return 1;
	const ak_case_run *r = &runs[lo - 1];
	uint32_t d = cp - r->start;
	if (d % r->stride == 0 && d / r->stride < r->count) out[0] = (uint32_t)((int32_t)cp + r->delta);
	return 1;
}
