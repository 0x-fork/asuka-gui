// words.c: Asuka's own words (platform/desktop's dsk_word: internal/l10n)
// in the app's language, where GTK and libadwaita have none: a back
// button's, a search's, an alert's OK, the main menu's Quit.
#include "host.h"

#include <stdio.h>

// The words handed out live until the eighth call after: long enough for
// the GTK call each is made for.
static char ring[8][512];
static unsigned at;

static char *slot(void) { return ring[at++ % AK_LEN(ring)]; }

// lx_word is a word of Asuka's by its key, in the app's language.
const char *lx_word(const char *key) { return dsk_word(S(H.locale), key); }

// lx_word_app is a word that names the app ({app}): "Quit Notes".
const char *lx_word_app(const char *key, const char *app) {
	char *out = slot();
	const char *vars[] = {"app", S(app)};
	dsk_word_format(out, sizeof ring[0], S(H.locale), key, vars, 2);
	return out;
}

// badge_number is a badge that's a count (digits only, as ops'
// BadgeNumber), or -1.
static long badge_number(const char *s) {
	size_t n = strlen(s);
	if (!n || n > 9) return -1;
	long v = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] < '0' || s[i] > '9') return -1;
		v = v * 10 + (s[i] - '0');
	}
	return v;
}

// lx_badged says a title with its badge for a screen reader: "Inbox, 3
// items" (a count as so many items, other text as it is).
const char *lx_badged(const char *title, const char *badge) {
	if (str_empty(badge)) return S(title);
	char *out = slot();
	long n = badge_number(badge);
	if (n < 0) {
		snprintf(out, sizeof ring[0], "%s, %s", S(title), badge);
		return out;
	}
	char count[16], said[256];
	snprintf(count, sizeof count, "%ld", n);
	const char *vars[] = {"count", count};
	dsk_word_format(said, sizeof said, S(H.locale), "badgeCount", vars, 2);
	snprintf(out, sizeof ring[0], "%s, %s", S(title), said);
	return out;
}
