// i18n.h: languages in the core (a port of package i18n's runtime side):
// BCP 47 tags, the direction a language is written in, matching the
// user's languages to the app's, regional formats, CLDR's plural rules,
// and catalogs: an app's messages, compiled (i18n's Catalog.Compile) and
// said in the language the app shows (locale.c).
#ifndef AK_UI_I18N_H
#define AK_UI_I18N_H

#include "rt/rt.h"
#include "ops/ops.h"

// ---- Tags ----

// ak_i18n_normalize writes a tag the canonical way ("zh-Hant-TW",
// "en-US"): "_" as "-", the language lower case, the script title case,
// the region upper case, deprecated languages replaced ("iw" is "he"), a
// POSIX locale's encoding and modifier dropped (en_US.UTF-8@euro). NULL
// for what names no language (owned otherwise).
ak_str *ak_i18n_normalize(ak_span tag);
// ak_i18n_rtl reports whether tag is written right to left: its script's
// direction, named or its language's likely one.
bool ak_i18n_rtl(ak_span tag);
bool ak_i18n_is_pseudo(ak_span tag);  // en-XA or ar-XB
// ak_i18n_resolve is the language of supported (normalized tags, the
// app's own first) to show a reader of preferred, or of override then
// preferred when the app chose one: a pseudo locale as it is, the best
// match, else supported's first. Owned.
ak_str *ak_i18n_resolve(const ak_str *override, const ak_arr *preferred, const ak_arr *supported);
// ak_i18n_match is the best of supported for preferred, or -1.
int ak_i18n_match(const ak_arr *preferred, const ak_arr *supported);
// ak_i18n_region_locale normalizes the locale of regional formats, whose
// region Apple's "rg" keyword changes (en_US@rg=gbzzzz is en-GB). Owned
// (empty for none).
ak_str *ak_i18n_region_locale(ak_span locale);
// CLDR's week and time data for a locale's region, or its language's
// likely one: the first day of the week (1 Sunday ... 7 Saturday), the
// hour cycle (12 or 24).
int ak_i18n_first_weekday(ak_span locale);
int ak_i18n_hour_cycle(ak_span locale);
// ak_i18n_format_locale is the locale a reader of lang with regional
// formats of region_locale writes numbers and dates in
// (en-GB-u-fw-mon-hc-h23). Owned.
ak_str *ak_i18n_format_locale(ak_span lang, ak_span region_locale, int first_weekday, int hour_cycle);
// ak_i18n_parent is a tag's parent for messages (CLDR's parent locales,
// then the tag without its last subtag), or empty at the root.
ak_span ak_i18n_parent(ak_span tag, char *buf, size_t n);

// ---- Plural rules (i18n_gen.c, from i18n/tables.go) ----

enum { AK_PLURAL_ZERO, AK_PLURAL_ONE, AK_PLURAL_TWO, AK_PLURAL_FEW, AK_PLURAL_MANY, AK_PLURAL_OTHER };

// A number's plural operands (UTS #35): integer digits, visible fraction
// digits with and without trailing zeros, those digits as integers, the
// compact exponent.
typedef struct {
	uint64_t i;
	int v, w;
	uint64_t f, t;
	int c;
} ak_operands;

ak_operands ak_operands_int(int64_t n);
// ak_operands_dec reads a decimal number as it's written ("1.50").
ak_operands ak_operands_dec(const char *s, size_t n);
int ak_plural(ak_span tag, const ak_operands *o, bool ordinal);

// The generated tables.
uint8_t ak_i18n_cardinal_rule(uint8_t rule, const ak_operands *o);
uint8_t ak_i18n_ordinal_rule(uint8_t rule, const ak_operands *o);
extern const char *const ak_i18n_cardinal_langs[];
extern const uint8_t ak_i18n_cardinal_rules[];
extern const uint32_t ak_i18n_ncardinal;
extern const char *const ak_i18n_ordinal_langs[];
extern const uint8_t ak_i18n_ordinal_rules[];
extern const uint32_t ak_i18n_nordinal;
// Week data by region: a region's first day (1 Sunday ... 7 Saturday)
// and its hour cycle; a language's likely region.
typedef struct {
	char region[4];
	uint8_t first_day, hour_cycle;
} ak_i18n_region_data;
extern const ak_i18n_region_data ak_i18n_regions[];
extern const uint32_t ak_i18n_nregions;
typedef struct {
	const char *lang;
	const char *script;  // its likely script
	char region[4];      // its likely region
} ak_i18n_likely;
extern const ak_i18n_likely ak_i18n_likelies[];
extern const uint32_t ak_i18n_nlikelies;
extern const char *const ak_i18n_parents[];  // child, parent, ...
extern const uint32_t ak_i18n_nparents;

// ---- Catalogs ----

typedef struct ak_catalog ak_catalog;  // an rt object (AK_TAG_OPAQUE)

// ak_i18n_catalog loads a compiled catalog (i18n's Catalog.Compile): its
// languages and messages. It's registered, replacing the one before (a
// reload's module registers its own): the app's languages are its.
// It throws on bytes that aren't one.
ak_catalog *ak_i18n_catalog(ak_bytes *blob);
// ak_i18n_say says message key (its index in the catalog's keys) with
// args (a tuple: its arguments in the message's order, ints, floats or
// strings as the catalog declares them), in the language the app shows
// (reading it follows it), or in lang when it isn't NULL. Owned.
ak_str *ak_i18n_say(ak_catalog *c, int64_t key, ak_val args, const ak_str *lang);
// ak_i18n_catalog_languages is the registered catalogs' languages, the
// first's source first (NULL: none). Owned.
ak_arr *ak_i18n_catalog_languages(void);
// Formatting in the language shown (tracked), with the registered
// catalogs' data for it (Latin digits and English's patterns without
// any), as std/i18n's functions do. flags: 1 percent, 2 integer, 4 no
// grouping, 8 half to even. A date is nanoseconds since the Unix epoch,
// in the device's zone, in styles 0 none, 1 short, 2 medium, 3 long, 4
// full, in the user's hour cycle. All owned.
ak_str *ak_i18n_format_number(double v, int flags, int min_frac, int max_frac);
ak_str *ak_i18n_format_int(int64_t v, int flags);
ak_str *ak_i18n_format_date(int64_t ns, int date_style, int time_style);
// ak_i18n_autonym is a language's name in itself ("Deutsch"), of the
// catalogs' languages; another tag as it is. Owned.
ak_str *ak_i18n_autonym(const ak_str *tag);
// ak_i18n_reset forgets the registered catalogs (tests, a reload).
void ak_i18n_reset(void);

#endif
