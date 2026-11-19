// html.c: std/html's builtins: escaping text for HTML, and reading
// character references back (numeric ones, and the named ones text
// uses: XML's five, Latin-1's, typography's, arrows, Greek letters).
#include "std/std.h"

#define STR(i) ((ak_str *)args[i].p)

// escape: & < > " ' as references, so the text is safe in an element's
// content and in a quoted attribute. The result is sized first (each
// character's reference's extra length), then the runs between them
// copied whole.
static const uint8_t extra[256] = {['&'] = 4, ['<'] = 3, ['>'] = 3, ['"'] = 5, ['\''] = 4};

static ak_val escape(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_str *s = STR(0);
	const uint8_t *p = (const uint8_t *)s->data;
	size_t len = s->len, more = 0;
	for (size_t i = 0; i < len; i++) more += extra[p[i]];
	if (!more) return (ak_val){.p = ak_retain(s)};
	ak_str *r = ak_str_new(NULL, len + more);
	char *o = r->data;
	size_t run = 0;
	for (size_t i = 0; i < len; i++) {
		if (!extra[p[i]]) continue;
		memcpy(o, p + run, i - run);
		o += i - run;
		run = i + 1;
		switch (p[i]) {
		case '&': memcpy(o, "&amp;", 5), o += 5; break;
		case '<': memcpy(o, "&lt;", 4), o += 4; break;
		case '>': memcpy(o, "&gt;", 4), o += 4; break;
		case '"': memcpy(o, "&quot;", 6), o += 6; break;
		default: memcpy(o, "&#39;", 5), o += 5; break;
		}
	}
	memcpy(o, p + run, len - run);
	return (ak_val){.p = r};
}

// Latin-1's names, U+00A0 to U+00FF in order.
static const char *const latin1[96] = {
	"nbsp", "iexcl", "cent", "pound", "curren", "yen", "brvbar", "sect", "uml", "copy", "ordf", "laquo", "not", "shy", "reg", "macr",
	"deg", "plusmn", "sup2", "sup3", "acute", "micro", "para", "middot", "cedil", "sup1", "ordm", "raquo", "frac14", "frac12", "frac34", "iquest",
	"Agrave", "Aacute", "Acirc", "Atilde", "Auml", "Aring", "AElig", "Ccedil", "Egrave", "Eacute", "Ecirc", "Euml", "Igrave", "Iacute", "Icirc", "Iuml",
	"ETH", "Ntilde", "Ograve", "Oacute", "Ocirc", "Otilde", "Ouml", "times", "Oslash", "Ugrave", "Uacute", "Ucirc", "Uuml", "Yacute", "THORN", "szlig",
	"agrave", "aacute", "acirc", "atilde", "auml", "aring", "aelig", "ccedil", "egrave", "eacute", "ecirc", "euml", "igrave", "iacute", "icirc", "iuml",
	"eth", "ntilde", "ograve", "oacute", "ocirc", "otilde", "ouml", "divide", "oslash", "ugrave", "uacute", "ucirc", "uuml", "yacute", "thorn", "yuml",
};

static const struct {
	const char *name;
	uint32_t cp;
} others[] = {
	{"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
	{"OElig", 0x152}, {"oelig", 0x153}, {"Scaron", 0x160}, {"scaron", 0x161}, {"Yuml", 0x178}, {"fnof", 0x192},
	{"circ", 0x2c6}, {"tilde", 0x2dc}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009}, {"zwnj", 0x200c},
	{"zwj", 0x200d}, {"lrm", 0x200e}, {"rlm", 0x200f}, {"ndash", 0x2013}, {"mdash", 0x2014}, {"lsquo", 0x2018},
	{"rsquo", 0x2019}, {"sbquo", 0x201a}, {"ldquo", 0x201c}, {"rdquo", 0x201d}, {"bdquo", 0x201e}, {"dagger", 0x2020},
	{"Dagger", 0x2021}, {"bull", 0x2022}, {"hellip", 0x2026}, {"permil", 0x2030}, {"prime", 0x2032}, {"Prime", 0x2033},
	{"lsaquo", 0x2039}, {"rsaquo", 0x203a}, {"oline", 0x203e}, {"frasl", 0x2044}, {"euro", 0x20ac}, {"trade", 0x2122},
	{"larr", 0x2190}, {"uarr", 0x2191}, {"rarr", 0x2192}, {"darr", 0x2193}, {"harr", 0x2194}, {"lArr", 0x21d0},
	{"rArr", 0x21d2}, {"hArr", 0x21d4}, {"minus", 0x2212}, {"infin", 0x221e}, {"ne", 0x2260}, {"le", 0x2264},
	{"ge", 0x2265}, {"asymp", 0x2248}, {"sum", 0x2211}, {"prod", 0x220f}, {"radic", 0x221a}, {"part", 0x2202},
	{"spades", 0x2660}, {"clubs", 0x2663}, {"hearts", 0x2665}, {"diams", 0x2666}, {"loz", 0x25ca}, {"nbhy", 0x2011},
	{"Alpha", 0x391}, {"Beta", 0x392}, {"Gamma", 0x393}, {"Delta", 0x394}, {"Epsilon", 0x395}, {"Zeta", 0x396},
	{"Eta", 0x397}, {"Theta", 0x398}, {"Iota", 0x399}, {"Kappa", 0x39a}, {"Lambda", 0x39b}, {"Mu", 0x39c},
	{"Nu", 0x39d}, {"Xi", 0x39e}, {"Omicron", 0x39f}, {"Pi", 0x3a0}, {"Rho", 0x3a1}, {"Sigma", 0x3a3},
	{"Tau", 0x3a4}, {"Upsilon", 0x3a5}, {"Phi", 0x3a6}, {"Chi", 0x3a7}, {"Psi", 0x3a8}, {"Omega", 0x3a9},
	{"alpha", 0x3b1}, {"beta", 0x3b2}, {"gamma", 0x3b3}, {"delta", 0x3b4}, {"epsilon", 0x3b5}, {"zeta", 0x3b6},
	{"eta", 0x3b7}, {"theta", 0x3b8}, {"iota", 0x3b9}, {"kappa", 0x3ba}, {"lambda", 0x3bb}, {"mu", 0x3bc},
	{"nu", 0x3bd}, {"xi", 0x3be}, {"omicron", 0x3bf}, {"pi", 0x3c0}, {"rho", 0x3c1}, {"sigmaf", 0x3c2},
	{"sigma", 0x3c3}, {"tau", 0x3c4}, {"upsilon", 0x3c5}, {"phi", 0x3c6}, {"chi", 0x3c7}, {"psi", 0x3c8},
	{"omega", 0x3c9},
};

static bool named(const char *s, size_t len, uint32_t *cp) {
	// XML's five first: most of what text has.
	if (len == 2 && s[1] == 't' && (s[0] == 'l' || s[0] == 'g')) return *cp = s[0] == 'l' ? '<' : '>', true;
	if (len == 3 && memcmp(s, "amp", 3) == 0) return *cp = '&', true;
	if (len == 4 && memcmp(s, "quot", 4) == 0) return *cp = '"', true;
	if (len == 4 && memcmp(s, "apos", 4) == 0) return *cp = '\'', true;
	for (size_t i = 0; i < AK_LEN(latin1); i++)
		if (strlen(latin1[i]) == len && memcmp(latin1[i], s, len) == 0) return *cp = 0xa0 + (uint32_t)i, true;
	for (size_t i = 0; i < AK_LEN(others); i++)
		if (strlen(others[i].name) == len && memcmp(others[i].name, s, len) == 0) return *cp = others[i].cp, true;
	return false;
}

// reference reads the reference at s[i] ('&'); its length (0: none) and
// code point.
static size_t reference(const char *s, size_t n, size_t i, uint32_t *cp) {
	size_t j = i + 1;
	if (j < n && s[j] == '#') {
		j++;
		bool hex = j < n && (s[j] == 'x' || s[j] == 'X');
		if (hex) j++;
		size_t start = j;
		uint64_t v = 0;
		for (; j < n; j++) {
			char c = s[j];
			int d = c >= '0' && c <= '9' ? c - '0' : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10 : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
			if (d < 0) break;
			if (v <= 0x10ffff) v = v * (hex ? 16 : 10) + (uint64_t)d;
		}
		if (j == start) return 0;
		if (j < n && s[j] == ';') j++;
		*cp = v == 0 || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff) ? 0xfffd : (uint32_t)v;
		return j - i;
	}
	size_t start = j;
	while (j < n && j - start < 16 && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z') || (s[j] >= '0' && s[j] <= '9'))) j++;
	if (j == start || j >= n || s[j] != ';' || !named(s + start, j - start, cp)) return 0;
	return j + 1 - i;
}

// unescape: references read as their characters; what isn't one stays.
static ak_val unescape(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	ak_str *s = STR(0);
	const char *amp = memchr(s->data, '&', s->len);
	if (!amp) return (ak_val){.p = ak_retain(s)};
	// A reference is never shorter than what it stands for (&#0; is
	// U+FFFD's 3 bytes in 4): the text's length is room enough.
	ak_str *r = ak_str_new(NULL, s->len);
	char *o = r->data;
	size_t i = 0, len = s->len;
	while (amp) {
		size_t at = (size_t)(amp - s->data);
		memcpy(o, s->data + i, at - i);
		o += at - i;
		uint32_t cp;
		size_t k = reference(s->data, len, at, &cp);
		if (k) {
			o += ak_utf8_encode(cp, o);
			i = at + k;
		} else {
			*o++ = '&';
			i = at + 1;
		}
		amp = i < len ? memchr(s->data + i, '&', len - i) : NULL;
	}
	memcpy(o, s->data + i, len - i);
	o += len - i;
	r->len = (uint32_t)(o - r->data);
	r->data[r->len] = 0;
	return (ak_val){.p = r};
}

const ak_lib_entry ak_std_html[] = {
	{"html.escape", escape},
	{"html.unescape", unescape},
	{NULL, NULL},
};
