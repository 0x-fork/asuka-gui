// words.c: Asuka's own words (internal/l10n: an alert's only button, a
// search field's placeholder, a back button, the updater's dialogs) in
// the app's language, the hosts' platforms having none of theirs where
// Asuka shows them (desktop.h).
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "desktop.h"
#include "words.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int key_index(const char *key) {
	int lo = 0, hi = DSK_WORD_KEYS - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2, c = strcmp(dsk_word_keys[mid], key);
		if (!c) return mid;
		if (c < 0) lo = mid + 1;
		else hi = mid - 1;
	}
	return -1;
}

static int lang_index(const char *tag) {
	for (int i = 0; i < DSK_WORD_LANGS; i++) {
		const char *a = dsk_word_langs[i].tag, *b = tag;
		while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)(*b == '_' ? '-' : *b))) a++, b++;
		if (!*a && !*b) return i;
	}
	return -1;
}

// resolve finds the catalog for a locale (en_US, zh-Hant-TW, pt-BR): the
// tag itself, Chinese by its script (or its region's), the language, else
// English.
static int resolve(const char *locale) {
	char tag[64];
	snprintf(tag, sizeof tag, "%s", locale && *locale ? locale : "en");
	for (char *p = tag; *p; p++)
		if (*p == '_') *p = '-';
	char *at = strchr(tag, '@');
	if (at) *at = 0;
	int i = lang_index(tag);
	if (i >= 0) return i;
	char lang[16];
	size_t n = strcspn(tag, "-");
	snprintf(lang, sizeof lang, "%.*s", (int)(n < 15 ? n : 15), tag);
	if (!strcmp(lang, "zh")) {
		const char *rest = tag + n;
		bool hant = strstr(rest, "Hant") || strstr(rest, "hant") || strstr(rest, "-TW") || strstr(rest, "-HK") || strstr(rest, "-MO");
		return lang_index(hant ? "zh-Hant" : "zh-Hans");
	}
	if (!strcmp(lang, "iw")) snprintf(lang, sizeof lang, "he");
	if (!strcmp(lang, "no") || !strcmp(lang, "nn")) snprintf(lang, sizeof lang, "nb");
	if (!strcmp(lang, "tl")) snprintf(lang, sizeof lang, "fil");
	i = lang_index(lang);
	return i >= 0 ? i : lang_index("en");
}

const char *dsk_word(const char *locale, const char *key) {
	int k = key_index(key);
	if (k < 0) return "";
	int l = resolve(locale);
	const char *w = l >= 0 ? dsk_word_langs[l].words[k] : NULL;
	if (!w) {
		int en = lang_index("en");
		w = en >= 0 ? dsk_word_langs[en].words[k] : NULL;
	}
	return w ? w : "";
}

static void put(char *out, size_t cap, size_t *at, const char *s, size_t n) {
	for (size_t i = 0; i < n && *at + 1 < cap; i++) out[(*at)++] = s[i];
	out[*at < cap ? *at : cap - 1] = 0;
}

// plural picks a plural message's case for count: =count, one for 1,
// else other (the cases Asuka's words use), # replaced by the count.
static void plural(char *out, size_t cap, size_t *at, const char *body, size_t len, long count) {
	char want[24];
	snprintf(want, sizeof want, "=%ld", count);
	const char *pick = NULL, *other = NULL;
	size_t pick_n = 0, other_n = 0;
	size_t i = 0;
	while (i < len) {
		while (i < len && body[i] == ' ') i++;
		size_t name = i;
		while (i < len && body[i] != ' ' && body[i] != '{') i++;
		size_t name_n = i - name;
		while (i < len && body[i] != '{') i++;
		if (i >= len) break;
		size_t start = ++i;
		int depth = 1;
		while (i < len && depth) {
			if (body[i] == '{') depth++;
			if (body[i] == '}') depth--;
			i++;
		}
		size_t n = i - start - 1;
		if (name_n == strlen(want) && !strncmp(body + name, want, name_n)) pick = body + start, pick_n = n;
		else if (!pick && count == 1 && name_n == 3 && !strncmp(body + name, "one", 3)) pick = body + start, pick_n = n;
		else if (name_n == 5 && !strncmp(body + name, "other", 5)) other = body + start, other_n = n;
	}
	if (!pick) pick = other, pick_n = other_n;
	char num[24];
	snprintf(num, sizeof num, "%ld", count);
	for (size_t j = 0; pick && j < pick_n; j++) {
		if (pick[j] == '#') put(out, cap, at, num, strlen(num));
		else put(out, cap, at, pick + j, 1);
	}
}

size_t dsk_word_format(char *out, size_t cap, const char *locale, const char *key, const char *const *vars, size_t nvars) {
	const char *w = dsk_word(locale, key);
	size_t at = 0;
	if (cap) out[0] = 0;
	for (const char *p = w; *p;) {
		if (*p != '{') {
			put(out, cap, &at, p++, 1);
			continue;
		}
		const char *end = p + 1;
		int depth = 1;
		while (*end && depth) {
			if (*end == '{') depth++;
			if (*end == '}') depth--;
			end++;
		}
		// {name} or {name, plural, ...}
		const char *in = p + 1;
		size_t in_n = (size_t)(end - p - 2);
		size_t name_n = strcspn(in, ",}");
		const char *value = NULL;
		for (size_t v = 0; v + 1 < nvars + 1 && v + 1 <= nvars; v += 2)
			if (strlen(vars[v]) == name_n && !strncmp(vars[v], in, name_n)) value = vars[v + 1];
		if (name_n < in_n) {
			const char *rest = in + name_n + 1;
			while (*rest == ' ') rest++;
			if (!strncmp(rest, "plural,", 7)) {
				rest += 7;
				plural(out, cap, &at, rest, (size_t)(in + in_n - rest), value ? strtol(value, NULL, 10) : 0);
			}
		} else if (value) {
			put(out, cap, &at, value, strlen(value));
		}
		p = end;
	}
	return at;
}
