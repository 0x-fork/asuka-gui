// i18n.c: CLDR's plural rules and catalogs: an app's messages, compiled
// (i18n's Catalog.Compile), said in the language the app shows.
#include "ui/i18n.h"
#include "ui/ui_ext.h"

ak_operands ak_operands_int(int64_t n) {
	uint64_t u = n < 0 ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n;
	const uint64_t big = UINT64_C(1000000000000000000);
	if (u >= big) u = u % big + big;
	return (ak_operands){.i = u};
}

static int search(const char *const *list, uint32_t n, ak_span s) {
	uint32_t lo = 0, hi = n;
	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;
		size_t ln = strlen(list[m]);
		int c = memcmp(list[m], s.p, ln < s.len ? ln : s.len);
		if (!c) c = ln < s.len ? -1 : ln > s.len ? 1 : 0;
		if (!c) return (int)m;
		if (c < 0) lo = m + 1;
		else hi = m;
	}
	return -1;
}

// rule_of is the rule set of tag: that of the tag, or of the tag without
// its last subtag, down to its language.
static uint8_t rule_of(ak_span tag, const char *const *langs, const uint8_t *rules, uint32_t n) {
	ak_str *t = ak_i18n_normalize(tag);
	ak_span s = ak_span_str(t);
	for (uint32_t i = 1; i < s.len; i++)
		if (s.p[i - 1] == '-' && s.p[i] != '-' && (i + 1 == s.len || s.p[i + 1] == '-')) s.len = i - 1;
	uint8_t r = 0;
	while (s.len) {
		int i = search(langs, n, s);
		if (i >= 0) {
			r = rules[i];
			break;
		}
		while (s.len && s.p[s.len - 1] != '-') s.len--;
		if (s.len) s.len--;
	}
	ak_release(t);
	return r;
}

int ak_plural(ak_span tag, const ak_operands *o, bool ordinal) {
	if (ordinal) return ak_i18n_ordinal_rule(rule_of(tag, ak_i18n_ordinal_langs, ak_i18n_ordinal_rules, ak_i18n_nordinal), o);
	return ak_i18n_cardinal_rule(rule_of(tag, ak_i18n_cardinal_langs, ak_i18n_cardinal_rules, ak_i18n_ncardinal), o);
}

ak_arr *ak_i18n_catalog_languages(void) { return NULL; }

ak_catalog *ak_i18n_catalog(ak_bytes *blob) {
	AK_UNUSED(blob);
	ak_throw_msg("i18n: catalogs aren't available yet");
	return NULL;
}

ak_str *ak_i18n_say(ak_catalog *c, int64_t key, ak_val args, const ak_str *lang) {
	AK_UNUSED(c), AK_UNUSED(key), AK_UNUSED(args), AK_UNUSED(lang);
	return ak_str_c("");
}

void ak_i18n_reset(void) {}
