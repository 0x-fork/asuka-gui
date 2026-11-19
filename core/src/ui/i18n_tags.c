// i18n_tags.c: BCP 47 tags in the core (i18n/i18n.go, parent.go,
// region.go, pseudo.go's IsPseudo, cldr.go's week and time data): what
// the locale model needs at run time, with CLDR's data from i18n_gen.c.
#include "ui/i18n.h"
#include "ui/ui_ext.h"

static bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool digit(char c) { return c >= '0' && c <= '9'; }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

static bool all(ak_span s, bool (*f)(char)) {
	for (uint32_t i = 0; i < s.len; i++)
		if (!f(s.p[i])) return false;
	return s.len > 0;
}

static bool alnum(char c) { return alpha(c) || digit(c); }

// Deprecated languages ICU and x/text replace.
static const char *const aliases[][2] = {{"iw", "he"}, {"in", "id"}, {"ji", "yi"}, {"jw", "jv"}};

// A parsed tag: its subtags' spans in the normalized string.
typedef struct {
	ak_span lang, script, region;
	const char *rest;  // variants and extensions, from their "-"
	uint32_t nrest;
} tag_parts;

ak_str *ak_i18n_normalize(ak_span in) {
	// Trim, drop a POSIX locale's encoding and modifier.
	while (in.len && (in.p[0] == ' ' || in.p[0] == '\t')) in.p++, in.len--;
	while (in.len && (in.p[in.len - 1] == ' ' || in.p[in.len - 1] == '\t')) in.len--;
	for (uint32_t i = 0; i < in.len; i++) {
		if (in.p[i] == '.' || in.p[i] == '@') {
			in.len = i;
			break;
		}
	}
	if (!in.len) return NULL;
	ak_strb b = {0};
	uint32_t at = 0, n = 0;
	bool ext = false;  // past a singleton: subtags as they are, lower case
	bool ok = true;
	while (at <= in.len && ok) {
		uint32_t end = at;
		while (end < in.len && in.p[end] != '-' && in.p[end] != '_') end++;
		ak_span s = {in.p + at, end - at};
		char buf[9];
		if (!s.len || s.len > 8 || !all(s, alnum)) {
			ok = false;
			break;
		}
		if (n == 0) {
			// The language: 2, 3 or 5 to 8 letters ("und" names none).
			if (!all(s, alpha) || s.len < 2 || s.len == 4) {
				ok = false;
				break;
			}
			for (uint32_t i = 0; i < s.len; i++) buf[i] = lower(s.p[i]);
			ak_span l = {buf, s.len};
			for (size_t k = 0; k < AK_LEN(aliases); k++)
				if (ak_span_eqc(l, aliases[k][0])) l = ak_cstr(aliases[k][1]);
			ak_strb_cstr(&b, l.p, l.len);
		} else {
			ak_strb_cstr(&b, "-", 1);
			if (s.len == 1) ext = true;
			for (uint32_t i = 0; i < s.len; i++) {
				char c = lower(s.p[i]);
				if (!ext && s.len == 4 && all(s, alpha)) c = i == 0 ? upper(s.p[i]) : lower(s.p[i]);       // a script
				else if (!ext && s.len == 2 && all(s, alpha)) c = upper(s.p[i]);                            // a region
				buf[i] = c;
			}
			ak_strb_cstr(&b, buf, s.len);
		}
		n++;
		at = end + 1;
	}
	ak_str *out = ak_strb_finish(&b);
	if (!ok || (out->len == 3 && memcmp(out->data, "und", 3) == 0)) {
		ak_release(out);
		return NULL;
	}
	return out;
}

static tag_parts parts_of(ak_span t) {
	tag_parts p = {0};
	uint32_t at = 0, n = 0;
	while (at <= t.len) {
		uint32_t end = at;
		while (end < t.len && t.p[end] != '-') end++;
		ak_span s = {t.p + at, end - at};
		if (n == 0) p.lang = s;
		else if (s.len == 4 && all(s, alpha) && !p.script.len && !p.region.len) p.script = s;
		else if ((s.len == 2 && all(s, alpha)) || (s.len == 3 && all(s, digit))) {
			if (!p.region.len) p.region = s;
		} else {
			p.rest = t.p + at - 1;
			p.nrest = t.len - (at - 1);
			break;
		}
		n++;
		at = end + 1;
	}
	return p;
}

static const ak_i18n_likely *likely(ak_span lang) {
	uint32_t lo = 0, hi = ak_i18n_nlikelies;
	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;
		const char *l = ak_i18n_likelies[m].lang;
		size_t ln = strlen(l);
		int c = memcmp(l, lang.p, ln < lang.len ? ln : lang.len);
		if (!c) c = ln < lang.len ? -1 : ln > lang.len ? 1 : 0;
		if (!c) return &ak_i18n_likelies[m];
		if (c < 0) lo = m + 1;
		else hi = m;
	}
	return NULL;
}

// script_of is t's script, or its language's likely one (in its region:
// zh-TW is Hant).
static ak_span script_of(const tag_parts *p) {
	if (p->script.len) return p->script;
	if (ak_span_eqc(p->lang, "zh") && (ak_span_eqc(p->region, "TW") || ak_span_eqc(p->region, "HK") || ak_span_eqc(p->region, "MO")))
		return AK_STR("Hant");
	if (ak_span_eqc(p->lang, "pa") && ak_span_eqc(p->region, "PK")) return AK_STR("Arab");
	if (ak_span_eqc(p->lang, "uz") && ak_span_eqc(p->region, "AF")) return AK_STR("Arab");
	const ak_i18n_likely *l = likely(p->lang);
	return l ? ak_cstr(l->script) : AK_STR("");
}

// The scripts of Unicode written right to left.
static const char *const rtl_scripts[] = {"Adlm", "Arab", "Aran", "Armi", "Avst", "Chrs", "Cprt", "Elym", "Hatr", "Hebr", "Hung", "Khar",
                                          "Lydi", "Mand", "Mani", "Mend", "Merc", "Mero", "Narb", "Nbat", "Nkoo", "Orkh", "Ougr", "Palm",
                                          "Phli", "Phlp", "Phnx", "Prti", "Rohg", "Samr", "Sarb", "Sogd", "Sogo", "Syrc", "Thaa", "Yezi"};

bool ak_i18n_rtl(ak_span tag) {
	ak_str *n = ak_i18n_normalize(tag);
	if (!n) return false;
	tag_parts p = parts_of(ak_span_str(n));
	ak_span s = script_of(&p);
	bool rtl = false;
	for (size_t i = 0; i < AK_LEN(rtl_scripts) && !rtl; i++) rtl = ak_span_eqc(s, rtl_scripts[i]);
	ak_release(n);
	return rtl;
}

static ak_span strip_ext(ak_span t) {
	for (uint32_t i = 1; i < t.len; i++)
		if (t.p[i - 1] == '-' && t.p[i] != '-' && (i + 1 == t.len || t.p[i + 1] == '-')) return (ak_span){t.p, i - 1};
	return t;
}

bool ak_i18n_is_pseudo(ak_span tag) {
	ak_str *n = ak_i18n_normalize(tag);
	if (!n) return false;
	ak_span s = strip_ext(ak_span_str(n));
	bool p = ak_span_eqc(s, "en-XA") || ak_span_eqc(s, "ar-XB");
	ak_release(n);
	return p;
}

// score is how well supported tag s suits preferred tag p: 0 not at all
// (another language, or script), more the closer.
static int score(const tag_parts *p, const tag_parts *s) {
	if (!ak_span_eq(p->lang, s->lang)) return 0;
	if (!ak_span_eq(script_of(p), script_of(s))) return 0;
	if (ak_span_eq(p->region, s->region)) return 4;
	if (!s->region.len) return 3;
	// A region the language is most likely spoken in, over another.
	const ak_i18n_likely *l = likely(s->lang);
	if (l && ak_span_eqc(s->region, l->region)) return 2;
	return 1;
}

int ak_i18n_match(const ak_arr *preferred, const ak_arr *supported) {
	for (uint32_t i = 0; preferred && i < preferred->len; i++) {
		ak_span pt = strip_ext(ak_span_str(ak_arr_vals(preferred)[i].p));
		tag_parts p = parts_of(pt);
		int best = -1, best_score = 0;
		for (uint32_t j = 0; supported && j < supported->len; j++) {
			ak_span st = strip_ext(ak_span_str(ak_arr_vals(supported)[j].p));
			if (ak_span_eq(st, pt)) return (int)j;
			tag_parts s = parts_of(st);
			int sc = score(&p, &s);
			if (sc > best_score) best = (int)j, best_score = sc;
		}
		if (best >= 0) return best;
	}
	return -1;
}

ak_str *ak_i18n_resolve(const ak_str *override, const ak_arr *preferred, const ak_arr *supported) {
	ak_arr *want = (ak_arr *)preferred;
	bool made = false;
	if (override && override->len) {
		if (ak_i18n_is_pseudo(ak_span_str(override))) return ak_retain((ak_str *)override);
		want = ak_arr_new(AK_K_REF, NULL, 0);
		want->h.flags = 1;
		ak_arr_push(&want, (ak_val){.p = ak_retain((ak_str *)override)});
		for (uint32_t i = 0; preferred && i < preferred->len; i++) ak_arr_push(&want, (ak_val){.p = ak_retain(ak_arr_vals(preferred)[i].p)});
		made = true;
	}
	int m = ak_i18n_match(want, supported);
	if (made) ak_release(want);
	if (m >= 0) return ak_retain(ak_arr_vals(supported)[m].p);
	return supported && supported->len ? ak_retain(ak_arr_vals(supported)[0].p) : ak_str_c("en");
}

ak_str *ak_i18n_region_locale(ak_span locale) {
	uint32_t at = 0;
	while (at < locale.len && locale.p[at] != '@') at++;
	if (at < locale.len) {
		// Apple's rg keyword: en_US@rg=gbzzzz has the formats of en-GB.
		ak_span kws = {locale.p + at + 1, locale.len - at - 1}, rg = {0};
		uint32_t i = 0;
		while (i < kws.len) {
			uint32_t end = i;
			while (end < kws.len && kws.p[end] != ';') end++;
			ak_span kw = {kws.p + i, end - i};
			if (kw.len > 3 && lower(kw.p[0]) == 'r' && lower(kw.p[1]) == 'g' && kw.p[2] == '=') rg = (ak_span){kw.p + 3, kw.len - 3};
			i = end + 1;
		}
		ak_str *base = ak_i18n_normalize((ak_span){locale.p, at});
		if (rg.len >= 2 && base) {
			tag_parts p = parts_of(ak_span_str(base));
			ak_strb b = {0};
			uint32_t keep = p.region.len ? (uint32_t)(p.region.p - base->data - 1) : base->len;
			ak_strb_cstr(&b, base->data, keep);
			char r[3] = {'-', upper(rg.p[0]), upper(rg.p[1])};
			ak_strb_cstr(&b, r, 3);
			ak_str *t = ak_strb_finish(&b);
			ak_str *n = ak_i18n_normalize(ak_span_str(t));
			ak_release(t);
			if (n) {
				ak_release(base);
				return n;
			}
		}
		ak_release(base);
		locale.len = at;
	}
	ak_str *n = ak_i18n_normalize(locale);
	return n ? n : ak_str_c("");
}

static const ak_i18n_region_data *region_data(ak_span region) {
	uint32_t lo = 0, hi = ak_i18n_nregions;
	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;
		const char *r = ak_i18n_regions[m].region;
		size_t rn = strlen(r);
		int c = memcmp(r, region.p, rn < region.len ? rn : region.len);
		if (!c) c = rn < region.len ? -1 : rn > region.len ? 1 : 0;
		if (!c) return &ak_i18n_regions[m];
		if (c < 0) lo = m + 1;
		else hi = m;
	}
	return NULL;
}

// region_or_likely is the locale's region, its language's likely one, or
// "001", the world's.
static const ak_i18n_region_data *region_for(ak_span locale) {
	ak_str *n = ak_i18n_normalize(locale);
	tag_parts p = n ? parts_of(ak_span_str(n)) : (tag_parts){0};
	ak_span r = p.region;
	char buf[4] = {0};
	if (!r.len && p.lang.len) {
		const ak_i18n_likely *l = likely(p.lang);
		if (l) {
			memcpy(buf, l->region, 4);
			r = ak_cstr(buf);
		}
	}
	const ak_i18n_region_data *d = r.len ? region_data(r) : NULL;
	ak_release(n);
	return d ? d : region_data(AK_STR("001"));
}

int ak_i18n_first_weekday(ak_span locale) {
	const ak_i18n_region_data *d = region_for(locale);
	return d ? d->first_day : 2;
}

int ak_i18n_hour_cycle(ak_span locale) {
	const ak_i18n_region_data *d = region_for(locale);
	return d ? d->hour_cycle : 24;
}

ak_str *ak_i18n_format_locale(ak_span lang, ak_span region_locale, int first_weekday, int hour_cycle) {
	ak_str *l = ak_i18n_is_pseudo(lang) ? NULL : ak_i18n_normalize(lang);
	if (!l) l = ak_str_c("en");
	tag_parts lp = parts_of(ak_span_str(l));
	ak_str *rl = ak_i18n_normalize(region_locale);
	tag_parts rp = rl ? parts_of(ak_span_str(rl)) : (tag_parts){0};
	ak_strb b = {0};
	ak_strb_cstr(&b, lp.lang.p, lp.lang.len);
	if (rp.region.len) {
		// The script the reader reads, named or implied (zh-HK reads Hant),
		// unless the new region implies it too.
		ak_span s = script_of(&lp);
		tag_parts implied = {.lang = lp.lang, .region = rp.region};
		if (lp.script.len || (s.len && !ak_span_eq(script_of(&implied), s))) {
			ak_strb_cstr(&b, "-", 1);
			ak_strb_cstr(&b, s.p, s.len);
		}
		ak_strb_cstr(&b, "-", 1);
		ak_strb_cstr(&b, rp.region.p, rp.region.len);
	} else {
		if (lp.script.len) ak_strb_cstr(&b, "-", 1), ak_strb_cstr(&b, lp.script.p, lp.script.len);
		if (lp.region.len) ak_strb_cstr(&b, "-", 1), ak_strb_cstr(&b, lp.region.p, lp.region.len);
	}
	if (hour_cycle == 12 || hour_cycle == 24) {
		static const char *const days[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
		int d = first_weekday >= 1 && first_weekday <= 7 ? first_weekday - 1 : 1;
		ak_strb_cstr(&b, "-u-fw-", 6);
		ak_strb_cstr(&b, days[d], 3);
		ak_strb_cstr(&b, hour_cycle == 12 ? "-hc-h12" : "-hc-h23", 7);
	}
	ak_release(l);
	ak_release(rl);
	return ak_strb_finish(&b);
}

ak_span ak_i18n_parent(ak_span tag, char *buf, size_t n) {
	ak_span t = strip_ext(tag);
	// CLDR's parent locales.
	for (uint32_t i = 0; i + 1 < ak_i18n_nparents * 2; i += 2) {
		if (ak_span_eqc(t, ak_i18n_parents[i])) return ak_cstr(ak_i18n_parents[i + 1]);
	}
	uint32_t dash = t.len;
	while (dash > 0 && t.p[dash - 1] != '-') dash--;
	if (!dash) return (ak_span){"", 0};
	tag_parts p = parts_of(t);
	if (p.script.len && !p.region.len && !p.nrest) {
		// language-Script: the root unless the script is the language's
		// usual one (CLDR's nonlikelyScript rule).
		const ak_i18n_likely *l = likely(p.lang);
		if (l && !ak_span_eqc(p.script, l->script)) return (ak_span){"", 0};
	}
	size_t m = dash - 1 < n ? dash - 1 : n;
	memcpy(buf, t.p, m);
	return (ak_span){buf, (uint32_t)m};
}
